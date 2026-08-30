#include "SPI.h"

/* SPI1 is on HB2; SPI2-4 are on HB1. Named in one place so no call site has to
 * remember, because the wrong bus is silent: the peripheral's registers read
 * back as zeroes and every write to them is discarded. */
static void spi_clock_enable(uint8_t id) {
    if (id == 1) {
        ch32h4_clock_enable(CH32_BUS_HB2, RCC_HB2Periph_SPI1);
        return;
    }
    static const uint32_t hb1[3] = {
        RCC_HB1Periph_SPI2, RCC_HB1Periph_SPI3, RCC_HB1Periph_SPI4,
    };
    ch32h4_clock_enable(CH32_BUS_HB1, hb1[id - 2]);
}

static void spi_reset(uint8_t id) {
    /* Configuration survives a warm reset, the debugger's reset and a
     * re-flash, so a peripheral that is not reset inherits the previous run's
     * mode and baud divider. */
    if (id == 1) {
        ch32h4_block_reset(CH32_BUS_HB2, RCC_HB2Periph_SPI1);
        return;
    }
    static const uint32_t hb1[3] = {
        RCC_HB1Periph_SPI2, RCC_HB1Periph_SPI3, RCC_HB1Periph_SPI4,
    };
    ch32h4_block_reset(CH32_BUS_HB1, hb1[id - 2]);
}

static SPI_TypeDef *spi_regs(uint8_t id) {
    switch (id) {
        case 1: return SPI1;
        case 2: return SPI2;
        case 3: return SPI3;
        default: return SPI4;
    }
}

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

    spi_clock_enable(_id);
    spi_reset(_id);

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
    SPI_TypeDef *dev = spi_regs(_id);

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
    SPI_Cmd(spi_regs(_id), DISABLE);
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
    SPI_TypeDef *dev = spi_regs(_id);

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

void SPIClassCH32H4::transfer(void *buf, size_t count) {
    uint8_t *p = (uint8_t *)buf;
    for (size_t i = 0; i < count; i++) {
        p[i] = transfer(p[i]);
    }
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
