#include "SPI.h"

#include "ch32h4_spi.h"

/* BR[2:0] divides HCLK by 2..256, giving 50 MHz down to 390.6 kHz.
 *
 * HCLK, not SystemCoreClock: the latter is four times larger on this core and
 * would silently ask for four times the clock the caller wanted. */
static uint16_t spi_baud_divider(uint32_t hclk, uint32_t wanted) {
    /* The SDK spells these SPI_BaudRatePrescaler_ModeN, where Mode0 is /2 and
     * Mode7 is /256 -- the field is simply N << 3. */
    for (uint8_t i = 0; i < 8; i++) {
        const uint32_t div = 2u << i;
        if (hclk / div <= wanted) {
            return (uint16_t)(i << 3);
        }
    }
    /* Slower than the slowest divider: hand back the slowest rather than
     * something faster than asked for. Too fast breaks a device; too slow only
     * makes it sluggish. */
    return SPI_BaudRatePrescaler_Mode7;
}

SPIClassCH32H4::SPIClassCH32H4(pin_size_t sck, pin_size_t miso, pin_size_t mosi)
    : _sck(sck), _miso(miso), _mosi(mosi) { }

bool SPIClassCH32H4::setSCK(pin_size_t pin) {
    if (_running) {
        return false;
    }
    _sck = pin;
    return ch32h4_spi_find(_sck, _miso, _mosi) != 0;
}

bool SPIClassCH32H4::setMISO(pin_size_t pin) {
    if (_running) {
        return false;
    }
    _miso = pin;
    return ch32h4_spi_find(_sck, _miso, _mosi) != 0;
}

bool SPIClassCH32H4::setMOSI(pin_size_t pin) {
    if (_running) {
        return false;
    }
    _mosi = pin;
    return ch32h4_spi_find(_sck, _miso, _mosi) != 0;
}

void SPIClassCH32H4::begin() {
    if (_running) {
        return;
    }

    _id = ch32h4_spi_find(_sck, _miso, _mosi);
    if (_id == 0) {
        /* No single peripheral can carry all three. Arduino's begin() returns
         * void, so this is reported through peripheral() == 0 rather than
         * thrown away entirely. */
        return;
    }

    ch32h4_spi_clock_enable(_id);
    ch32h4_spi_reset(_id);

    uint8_t af = 0;
    ch32h4_spi_sck_af(_id, _sck, &af);
    ch32h4_pin_af(g_pins[_sck].port, g_pins[_sck].bit, af, CH32H4_CFG_AF_PP_50);

    ch32h4_spi_mosi_af(_id, _mosi, &af);
    ch32h4_pin_af(g_pins[_mosi].port, g_pins[_mosi].bit, af, CH32H4_CFG_AF_PP_50);

    if (_miso != (pin_size_t)-1 && ch32h4_spi_miso_af(_id, _miso, &af)) {
        /* MISO is an input the peripheral reads, and it must still be an
         * alternate function rather than a floating input: the mux owns the
         * pad's output enable, and a floating input leaves the peripheral
         * disconnected while every status flag reads correctly. */
        ch32h4_pin_af(g_pins[_miso].port, g_pins[_miso].bit, af,
                      CH32H4_CFG_AF_PP_50);
    }

    _running = true;
    applySettings(arduino::SPISettings(_clock, (BitOrder)_bitOrder,
                                       _dataMode));
}

void SPIClassCH32H4::applySettings(const arduino::SPISettings &settings) {
    if (!_running) {
        return;
    }
    SPI_TypeDef *dev = ch32h4_spi_regs(_id);

    _clock = settings.getClockFreq();
    _bitOrder = settings.getBitOrder();
    _dataMode = settings.getDataMode();

    SPI_InitTypeDef init = {};
    init.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    init.SPI_Mode = SPI_Mode_Master;
    init.SPI_DataSize = SPI_DataSize_8b;
    init.SPI_CPOL = (_dataMode == SPI_MODE2 || _dataMode == SPI_MODE3)
                    ? SPI_CPOL_High : SPI_CPOL_Low;
    init.SPI_CPHA = (_dataMode == SPI_MODE1 || _dataMode == SPI_MODE3)
                    ? SPI_CPHA_2Edge : SPI_CPHA_1Edge;
    /* Software NSS. Arduino drives chip select from the sketch, and leaving
     * hardware NSS on would have the peripheral pull itself out of master mode
     * the moment the pin went low. */
    init.SPI_NSS = SPI_NSS_Soft;
    init.SPI_BaudRatePrescaler = spi_baud_divider(ch32h4_hclk(), _clock);
    init.SPI_FirstBit = (_bitOrder == LSBFIRST) ? SPI_FirstBit_LSB
                                                : SPI_FirstBit_MSB;
    init.SPI_CRCPolynomial = 7;

    SPI_Init(dev, &init);
    SPI_NSSInternalSoftwareConfig(dev, SPI_NSSInternalSoft_Set);
    SPI_Cmd(dev, ENABLE);
}

void SPIClassCH32H4::end() {
    if (!_running) {
        return;
    }
    SPI_Cmd(ch32h4_spi_regs(_id), DISABLE);
    _running = false;
}

void SPIClassCH32H4::beginTransaction(arduino::SPISettings settings) {
    if (!_running) {
        begin();
    }
    _inTransaction = true;
    applySettings(settings);
}

void SPIClassCH32H4::endTransaction() {
    _inTransaction = false;
}

uint8_t SPIClassCH32H4::transfer(uint8_t data) {
    if (!_running) {
        return 0;
    }
    SPI_TypeDef *dev = ch32h4_spi_regs(_id);

    while (SPI_I2S_GetFlagStatus(dev, SPI_I2S_FLAG_TXE) == RESET) {
    }
    SPI_I2S_SendData(dev, data);
    while (SPI_I2S_GetFlagStatus(dev, SPI_I2S_FLAG_RXNE) == RESET) {
    }
    return (uint8_t)SPI_I2S_ReceiveData(dev);
}

uint16_t SPIClassCH32H4::transfer16(uint16_t data) {
    /* Two byte transfers rather than switching the peripheral to 16-bit, so
     * the byte order is ours to define and matches every other Arduino core:
     * most significant byte first regardless of the bit order. */
    if (_bitOrder == LSBFIRST) {
        uint16_t lo = transfer((uint8_t)(data & 0xFF));
        uint16_t hi = transfer((uint8_t)(data >> 8));
        return (uint16_t)((hi << 8) | lo);
    }
    uint16_t hi = transfer((uint8_t)(data >> 8));
    uint16_t lo = transfer((uint8_t)(data & 0xFF));
    return (uint16_t)((hi << 8) | lo);
}

/* ---- block transfers ----------------------------------------------------
 *
 * The byte-at-a-time loop is far slower than the bus. Measured on this part at
 * 400 MHz, 4096 bytes in place:
 *
 *     1 MHz clock     607 kbit/s    60% of the bus
 *     8 MHz clock    1700 kbit/s    21%
 *    24 MHz clock    2269 kbit/s     9%
 *
 * It plateaus near 2.3 Mbit/s because every byte costs two polled waits, so
 * raising the clock past about 3 MHz buys nothing: the wire idles nine tenths
 * of the time and only the length of the idle changes.
 *
 * DMA moves the block with the CPU out of the way, so the rate is the bus
 * rate. This follows the MicroPython port's driver for the same silicon
 * closely, because several of its details are not things a reading of the
 * reference manual would suggest and all of them cost a byte or a hang:
 *
 *   - Arm the RECEIVE channel before the transmit one. WCH's own example does
 *     the opposite, and at these clocks the first byte can complete before the
 *     receiver is listening.
 *   - Receive outranks transmit in DMA priority. A byte collected late is
 *     lost; a byte loaded late only leaves SCK idle for a moment.
 *   - Drain a stale RXNE before arming, or the first "received" byte is one
 *     left over from the previous transfer.
 *   - Time out on LACK OF PROGRESS in CNTR, not on a fixed spin. A wrong
 *     DMAMUX request number gives a channel nothing ever triggers, and a fixed
 *     bound either fires early on a long slow transfer or hangs for a
 *     visible age on a short one.
 *
 * Channels 2 (RX) and 3 (TX): I2S owns 4 and 5, ADCInput owns 7.
 */

/* Reference manual table 10-2, as used by the MicroPython port's SPI driver:
 * SPI1 is 63/64 and each further bus is two higher -- which is what makes the
 * 65/66 the I2S code already uses for SPI2 come out right. */
#define SPI_DMA_TX_REQ(id)  (61 + 2 * (id))
#define SPI_DMA_RX_REQ(id)  (62 + 2 * (id))

#define SPI_DMA_RX_CH       DMA1_Channel2
#define SPI_DMA_TX_CH       DMA1_Channel3
#define SPI_DMA_RX_MUX      DMA_MuxChannel2
#define SPI_DMA_TX_MUX      DMA_MuxChannel3
/* Channel 2 occupies bits 4-7 of INTFR, channel 3 bits 8-11. */
#define SPI_DMA_RX_FLAGS    0x000000F0u
#define SPI_DMA_TX_FLAGS    0x00000F00u

/* CNTR is 16 bits, so a longer block is split rather than truncated. */
#define SPI_DMA_MAX         65535u

/* Microseconds without CNTR moving before a transfer is called dead. */
#define SPI_DMA_STALL_US    100000u

/* Clocked out when the caller only wants to receive, and written to when it
 * only wants to send. One byte either way, with the increment disabled. */
static uint8_t s_dma_idle = 0xFF;

static inline void spi_drain(SPI_TypeDef *dev) {
    if (dev->STATR & SPI_STATR_RXNE) {
        (void)dev->DATAR;
    }
    (void)dev->STATR;
}

void SPIClassCH32H4::transferPolled(const uint8_t *tx, uint8_t *rx,
                                    size_t count) {
    SPI_TypeDef *dev = ch32h4_spi_regs(_id);
    for (size_t i = 0; i < count; i++) {
        while (SPI_I2S_GetFlagStatus(dev, SPI_I2S_FLAG_TXE) == RESET) {
        }
        SPI_I2S_SendData(dev, tx ? tx[i] : 0xFF);
        while (SPI_I2S_GetFlagStatus(dev, SPI_I2S_FLAG_RXNE) == RESET) {
        }
        const uint8_t got = (uint8_t)SPI_I2S_ReceiveData(dev);
        if (rx) {
            rx[i] = got;
        }
    }
}

/* Watch one channel's CNTR to zero. False if it stops making progress, which
 * is what a mis-routed request line looks like from here. */
static bool dma_wait(DMA_Channel_TypeDef *ch) {
    uint32_t left = ch->CNTR;
    uint32_t deadline = micros() + SPI_DMA_STALL_US;
    while (ch->CNTR != 0) {
        const uint32_t now = ch->CNTR;
        if (now != left) {
            left = now;
            deadline = micros() + SPI_DMA_STALL_US;
        } else if ((int32_t)(micros() - deadline) > 0) {
            return false;
        }
    }
    return true;
}

bool SPIClassCH32H4::transferDMA(const uint8_t *tx, uint8_t *rx, size_t count) {
    SPI_TypeDef *dev = ch32h4_spi_regs(_id);
    bool ok = true;

    RCC_HBPeriphClockCmd(RCC_HBPeriph_DMA1, ENABLE);
    DMA_MuxChannelConfig(SPI_DMA_TX_MUX, SPI_DMA_TX_REQ(_id));
    DMA_MuxChannelConfig(SPI_DMA_RX_MUX, SPI_DMA_RX_REQ(_id));

    while (count != 0 && ok) {
        const uint16_t chunk =
            (uint16_t)(count > SPI_DMA_MAX ? SPI_DMA_MAX : count);

        DMA_Cmd(SPI_DMA_TX_CH, DISABLE);
        DMA_Cmd(SPI_DMA_RX_CH, DISABLE);
        DMA1->INTFCR = SPI_DMA_TX_FLAGS | SPI_DMA_RX_FLAGS;

        DMA_InitTypeDef d = {0};
        d.DMA_PeripheralBaseAddr = (uint32_t)&dev->DATAR;
        d.DMA_PeripheralInc = DMA_PeripheralInc_Disable;
        d.DMA_BufferSize = chunk;

        if (rx) {
            d.DMA_DIR = DMA_DIR_PeripheralSRC;
            d.DMA_Memory0BaseAddr = (uint32_t)rx;
            d.DMA_MemoryInc = DMA_MemoryInc_Enable;
            d.DMA_Priority = DMA_Priority_VeryHigh;
            DMA_Init(SPI_DMA_RX_CH, &d);
        }

        /* In place is safe without a copy: a byte must reach DATAR before it
         * can be shifted out, and the byte it exchanges only lands two byte
         * times later, so the transmit side stays ahead of the receive side
         * overwriting it. */
        d.DMA_DIR = DMA_DIR_PeripheralDST;
        d.DMA_Memory0BaseAddr = tx ? (uint32_t)tx : (uint32_t)&s_dma_idle;
        d.DMA_MemoryInc = tx ? DMA_MemoryInc_Enable : DMA_MemoryInc_Disable;
        d.DMA_Priority = DMA_Priority_High;
        DMA_Init(SPI_DMA_TX_CH, &d);

        spi_drain(dev);

        DMA_Channel_TypeDef *watch = SPI_DMA_TX_CH;
        if (rx) {
            DMA_Cmd(SPI_DMA_RX_CH, ENABLE);
            SPI_I2S_DMACmd(dev, SPI_I2S_DMAReq_Rx, ENABLE);
            watch = SPI_DMA_RX_CH;
        }
        SPI_I2S_DMACmd(dev, SPI_I2S_DMAReq_Tx, ENABLE);
        DMA_Cmd(SPI_DMA_TX_CH, ENABLE);

        ok = dma_wait(watch);

        /* Transmit complete means the last byte reached the shift register,
         * not the wire. Wait for the bus to go idle before dropping the
         * request lines, or a send-only transfer returns with a byte still
         * going out and the next endTransaction() raises CS under it. */
        if (ok && !rx) {
            uint32_t guard = 100000u;
            while ((dev->STATR & SPI_STATR_BSY) && guard) {
                guard--;
            }
        }

        SPI_I2S_DMACmd(dev, SPI_I2S_DMAReq_Tx, DISABLE);
        SPI_I2S_DMACmd(dev, SPI_I2S_DMAReq_Rx, DISABLE);
        DMA_Cmd(SPI_DMA_TX_CH, DISABLE);
        DMA_Cmd(SPI_DMA_RX_CH, DISABLE);
        DMA1->INTFCR = SPI_DMA_TX_FLAGS | SPI_DMA_RX_FLAGS;

        count -= chunk;
        if (tx) {
            tx += chunk;
        }
        if (rx) {
            rx += chunk;
        }
    }
    return ok;
}

void SPIClassCH32H4::transfer(const void *tx, void *rx, size_t count) {
    if (!_running || count == 0) {
        return;
    }
    const uint8_t *t = (const uint8_t *)tx;
    uint8_t *r = (uint8_t *)rx;

    if (count >= DMA_THRESHOLD && transferDMA(t, r, count)) {
        return;
    }
    /* Short, or the DMA stalled. Falling back keeps the transfer correct on a
     * board where something else has taken the channels. */
    transferPolled(t, r, count);
}

void SPIClassCH32H4::transfer(void *buf, size_t count) {
    transfer(buf, buf, count);
}

/* Arduino's interrupt-masking helpers. This core does not disable interrupts
 * around transfers -- transfer() is a blocking poll with no state an ISR could
 * corrupt -- so these are accepted and do nothing, which is what the AVR-era
 * API means on a part with no shared SPI state. */
void SPIClassCH32H4::usingInterrupt(int interruptNumber) { (void)interruptNumber; }
void SPIClassCH32H4::notUsingInterrupt(int interruptNumber) { (void)interruptNumber; }
void SPIClassCH32H4::attachInterrupt() { }
void SPIClassCH32H4::detachInterrupt() { }

SPIClassCH32H4 SPI;
