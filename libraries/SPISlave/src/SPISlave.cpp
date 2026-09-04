#include "SPISlave.h"

#include "ch32h4_spi.h"

SPISlaveClass SPISlave;

SPISlaveClass::SPISlaveClass(pin_size_t sck, pin_size_t miso, pin_size_t mosi,
                             pin_size_t cs)
    : _sck(sck), _miso(miso), _mosi(mosi), _cs(cs) { }

bool SPISlaveClass::resolve() {
    return ch32h4_spi_find(_sck, _miso, _mosi) != 0;
}

bool SPISlaveClass::setSCK(pin_size_t pin) {
    if (_running) { return false; }
    _sck = pin;
    return resolve();
}

bool SPISlaveClass::setMISO(pin_size_t pin) {
    if (_running) { return false; }
    _miso = pin;
    return resolve();
}

bool SPISlaveClass::setMOSI(pin_size_t pin) {
    if (_running) { return false; }
    _mosi = pin;
    return resolve();
}

bool SPISlaveClass::setCS(pin_size_t pin) {
    if (_running) { return false; }
    _cs = pin;
    /* Not an error to name a pin this part cannot use as NSS -- the slave
     * falls back to software chip select and says so through hardwareCS().
     * Reporting it here as a failure would make an otherwise working
     * configuration look broken. */
    return true;
}

/* The C-side trampolines. Two, because the two interrupts mean different
 * things: one is a byte moving, the other is a frame ending. */
static void spislave_irq_trampoline(uint8_t id, void *ctx) {
    (void)id;
    static_cast<SPISlaveClass *>(ctx)->handleIRQ();
}

static void spislave_cs_trampoline(void *ctx) {
    static_cast<SPISlaveClass *>(ctx)->handleCSRise();
}

bool SPISlaveClass::begin(arduino::SPISettings settings) {
    if (_running) {
        return false;
    }

    _id = ch32h4_spi_find(_sck, _miso, _mosi);
    if (_id == 0) {
        return false;
    }

    _bitOrder = settings.getBitOrder();
    _dataMode = settings.getDataMode();

    ch32h4_spi_clock_enable(_id);
    ch32h4_spi_reset(_id);

    /* Every pin goes through the AF mux, the inputs included. The mux owns the
     * pad's output enable, so a pin left as a plain input leaves the
     * peripheral disconnected while every status flag still reads correctly --
     * a slave that receives nothing and reports no error. */
    uint8_t af = 0;
    ch32h4_spi_sck_af(_id, _sck, &af);
    ch32h4_pin_af(g_pins[_sck].port, g_pins[_sck].bit, af, CH32H4_CFG_AF_PP_50);

    ch32h4_spi_mosi_af(_id, _mosi, &af);
    ch32h4_pin_af(g_pins[_mosi].port, g_pins[_mosi].bit, af,
                  CH32H4_CFG_AF_PP_50);

    ch32h4_spi_miso_af(_id, _miso, &af);
    ch32h4_pin_af(g_pins[_miso].port, g_pins[_miso].bit, af,
                  CH32H4_CFG_AF_PP_50);

    _hardCS = false;
    if (_cs != (pin_size_t)-1 && ch32h4_spi_nss_af(_id, _cs, &af)) {
        ch32h4_pin_af(g_pins[_cs].port, g_pins[_cs].bit, af,
                      CH32H4_CFG_AF_PP_50);
        _hardCS = true;
    }

    SPI_InitTypeDef init = {0};
    init.SPI_Direction = SPI_Direction_2Lines_FullDuplex;
    init.SPI_Mode = SPI_Mode_Slave;
    init.SPI_DataSize = SPI_DataSize_8b;
    init.SPI_CPOL = (_dataMode == SPI_MODE2 || _dataMode == SPI_MODE3)
                        ? SPI_CPOL_High : SPI_CPOL_Low;
    init.SPI_CPHA = (_dataMode == SPI_MODE1 || _dataMode == SPI_MODE3)
                        ? SPI_CPHA_2Edge : SPI_CPHA_1Edge;
    init.SPI_NSS = _hardCS ? SPI_NSS_Hard : SPI_NSS_Soft;
    /* Ignored in slave mode -- the master's clock sets the rate -- but the
     * field is not optional and a zero here is not a legal encoding. */
    init.SPI_BaudRatePrescaler = SPI_BaudRatePrescaler_Mode0;
    init.SPI_FirstBit = (_bitOrder == LSBFIRST) ? SPI_FirstBit_LSB
                                                : SPI_FirstBit_MSB;
    init.SPI_CRCPolynomial = 7;

    SPI_TypeDef *dev = ch32h4_spi_regs(_id);
    SPI_Init(dev, &init);

    if (!_hardCS) {
        /* Software NSS with SSI clear means permanently selected. Leaving SSI
         * set instead would be a slave that never responds -- the peripheral
         * reads its own select from that bit and a high one means "not you". */
        SPI_NSSInternalSoftwareConfig(dev, SPI_NSSInternalSoft_Reset);
    }

    _txLen = _txIndex = 0;
    _rxLen = 0;
    _sentFired = true;

    ch32h4_spi_attach_irq(_id, spislave_irq_trampoline, this);
    SPI_I2S_ITConfig(dev, SPI_I2S_IT_RXNE, ENABLE);
    NVIC_EnableIRQ(ch32h4_spi_irqn(_id));

    SPI_Cmd(dev, ENABLE);

    if (_hardCS) {
        /* The end of a frame, which the SPI peripheral itself cannot report:
         * this part has no slave-select-rise interrupt. EXTI on the same pin
         * gives one, and it composes with the AF mux because EXTI taps the
         * input data register whatever the pad's mode is. */
        attachInterruptParam(_cs, spislave_cs_trampoline, RISING, this);
    }

    _running = true;
    return true;
}

bool SPISlaveClass::end() {
    if (!_running) {
        return false;
    }
    SPI_TypeDef *dev = ch32h4_spi_regs(_id);
    SPI_Cmd(dev, DISABLE);
    ch32h4_spi_detach_irq(_id);
    NVIC_DisableIRQ(ch32h4_spi_irqn(_id));
    if (_hardCS) {
        detachInterrupt(_cs);
    }
    _running = false;
    _hardCS = false;
    return true;
}

size_t SPISlaveClass::setData(const uint8_t *data, size_t len) {
    if (!_running || data == nullptr) {
        return 0;
    }
    if (len > BUFFER_LENGTH) {
        len = BUFFER_LENGTH;
    }

    SPI_TypeDef *dev = ch32h4_spi_regs(_id);

    /* The interrupt walks _txIndex through this buffer, so it must not be
     * running while the buffer is replaced. Masking the source is enough and
     * is cheaper than a global critical section; a byte clocked in during
     * these few instructions still sets RXNE and is picked up on re-enable. */
    SPI_I2S_ITConfig(dev, SPI_I2S_IT_TXE, DISABLE);

    for (size_t i = 0; i < len; i++) {
        _txBuf[i] = data[i];
    }
    _txLen = len;
    _txIndex = 0;
    _sentFired = (len == 0);

    /* Preload the shift register. The first byte has to be there BEFORE the
     * master's first clock edge; waiting for TXE to interrupt would mean the
     * first byte of every frame is whatever the previous one left behind. */
    if (len > 0 && SPI_I2S_GetFlagStatus(dev, SPI_I2S_FLAG_TXE) == SET) {
        dev->DATAR = _txBuf[_txIndex++];
    }
    if (_txIndex < _txLen) {
        SPI_I2S_ITConfig(dev, SPI_I2S_IT_TXE, ENABLE);
    }
    return len;
}

void SPISlaveClass::handleIRQ() {
    SPI_TypeDef *dev = ch32h4_spi_regs(_id);

    if (SPI_I2S_GetITStatus(dev, SPI_I2S_IT_RXNE) == SET) {
        /* Reading DATAR is what clears RXNE; there is no separate
         * acknowledgement, so this read has to happen even when the buffer is
         * full or the interrupt re-enters forever. */
        const uint8_t b = (uint8_t)dev->DATAR;
        if (_rxLen < BUFFER_LENGTH) {
            _rxBuf[_rxLen++] = b;
        }

        if (!_hardCS && _txLen > 0 && _rxLen >= _txLen) {
            /* No chip select to mark the end of a frame, so the queued length
             * stands in for one. Documented in the header, and the reason a
             * CS pin is worth having. */
            handleCSRise();
        }
    }

    if (SPI_I2S_GetITStatus(dev, SPI_I2S_IT_TXE) == SET) {
        if (_txIndex < _txLen) {
            dev->DATAR = _txBuf[_txIndex++];
        }
        if (_txIndex >= _txLen) {
            /* Nothing left to send. TXE stays set for as long as the register
             * is empty, so the source must be masked or this becomes an
             * interrupt that never stops arriving. */
            SPI_I2S_ITConfig(dev, SPI_I2S_IT_TXE, DISABLE);
            if (!_sentFired) {
                _sentFired = true;
                if (_sentCb) {
                    _sentCb();
                }
            }
        }
    }
}

void SPISlaveClass::handleCSRise() {
    if (_rxLen == 0) {
        return;
    }
    const size_t len = _rxLen;
    _rxLen = 0;

    /* Reset for the next frame before the callback, not after: with hardware
     * NSS the master may start clocking again the moment CS falls, and a
     * callback that runs long would otherwise have its first bytes counted
     * into the frame that just ended. */
    _txIndex = 0;
    _sentFired = true;

    if (_recvCb) {
        _recvCb(_rxBuf, len);
    }
}
