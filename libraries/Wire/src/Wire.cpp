#include "Wire.h"

#include "ch32h4_i2c.h"

TwoWire::TwoWire(pin_size_t scl, pin_size_t sda) : _scl(scl), _sda(sda) { }

bool TwoWire::setSCL(pin_size_t pin) {
    if (_running) {
        return false;
    }
    _scl = pin;
    return ch32h4_i2c_find(_scl, _sda, nullptr) != 0;
}

bool TwoWire::setSDA(pin_size_t pin) {
    if (_running) {
        return false;
    }
    _sda = pin;
    return ch32h4_i2c_find(_scl, _sda, nullptr) != 0;
}

bool TwoWire::recover() {
    /* Take the pins away from the peripheral and drive them as open-drain
     * outputs, so the pull-ups do the pulling high -- push-pull here would
     * fight whatever device is holding the line and could damage it. */
    const ch32h4_pin_t *scl = &g_pins[_scl];
    const ch32h4_pin_t *sda = &g_pins[_sda];

    ch32h4_pin_af(scl->port, scl->bit, CH32H4_AF_NONE, CH32H4_CFG_OUT_OD_50);
    ch32h4_pin_af(sda->port, sda->bit, CH32H4_AF_NONE, CH32H4_CFG_OUT_OD_50);
    scl->port->BSHR = (1u << scl->bit);
    sda->port->BSHR = (1u << sda->bit);
    delayMicroseconds(10);

    /* Nine clocks: a full byte plus the ACK. That is enough for any device to
     * finish the transfer it thinks is in progress and release SDA. */
    for (int i = 0; i < 9; i++) {
        scl->port->BCR = (1u << scl->bit);
        delayMicroseconds(5);
        scl->port->BSHR = (1u << scl->bit);
        delayMicroseconds(5);
        if (sda->port->INDR & (1u << sda->bit)) {
            break;   /* the device let go */
        }
    }

    /* A STOP, so any device still following along sees a clean end. */
    sda->port->BCR = (1u << sda->bit);
    delayMicroseconds(5);
    scl->port->BSHR = (1u << scl->bit);
    delayMicroseconds(5);
    sda->port->BSHR = (1u << sda->bit);
    delayMicroseconds(10);

    const bool freed = (sda->port->INDR & (1u << sda->bit)) != 0;

    /* Then reset the peripheral -- the reference manual's own recommendation
     * -- and put the pins back. Its BUSY latch survives a plain re-init, so
     * without the reset the bus stays "busy" no matter what the wires do. */
    ch32h4_i2c_reset(_id);
    configure();

    return freed;
}

void TwoWire::configure() {
    ch32h4_i2c_clock_enable(_id);

    /* Alternate function, open-drain. There is no internal pull-up in this
     * mode on this part -- the F1-style encoding does not offer one -- so both
     * lines need real resistors. */
    ch32h4_pin_af(g_pins[_scl].port, g_pins[_scl].bit, _af, CH32H4_CFG_AF_OD_50);
    ch32h4_pin_af(g_pins[_sda].port, g_pins[_sda].bit, _af, CH32H4_CFG_AF_OD_50);

    I2C_InitTypeDef init = {};
    init.I2C_Mode = I2C_Mode_I2C;
    init.I2C_DutyCycle = I2C_DutyCycle_2;
    init.I2C_OwnAddress1 = 0x00;
    init.I2C_Ack = I2C_Ack_Enable;
    init.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    /* I2C_Init divides HCLK, and is correct as shipped -- unlike the ADC's
     * prescaler constants. */
    init.I2C_ClockSpeed = _clock;

    I2C_Init(ch32h4_i2c_regs(_id), &init);
    I2C_Cmd(ch32h4_i2c_regs(_id), ENABLE);
}

void TwoWire::begin() {
    if (_running) {
        return;
    }
    _id = ch32h4_i2c_find(_scl, _sda, &_af);
    if (_id == 0) {
        return;   /* not a pair the silicon offers; peripheral() reports it */
    }

    /* Reset before configuring. The BUSY latch survives a warm reset, the
     * debugger's reset and a re-flash, so a board reset in the middle of a
     * transfer would otherwise come back with a bus that is permanently busy
     * and no way to tell that from a missing device. */
    ch32h4_i2c_reset(_id);
    configure();
    _running = true;

    if (I2C_GetFlagStatus(ch32h4_i2c_regs(_id), I2C_FLAG_BUSY) != RESET) {
        recover();
    }
}

/* The C-side trampoline out of the core's interrupt dispatch. */
static void wire_irq_trampoline(uint8_t id, bool error, void *ctx) {
    (void)id;
    static_cast<TwoWire *>(ctx)->handleEvent(error);
}

void TwoWire::begin(uint8_t address) {
    if (_running) {
        return;
    }
    _id = ch32h4_i2c_find(_scl, _sda, &_af);
    if (_id == 0) {
        return;   /* not a pair the silicon offers; peripheral() reports it */
    }
    _slaveAddr = address;

    /* Reset first: the BUSY latch survives a warm reset and a re-flash, so a
     * board reset in the middle of a transfer otherwise comes back with a
     * peripheral that never matches an address again. */
    ch32h4_i2c_reset(_id);
    ch32h4_i2c_clock_enable(_id);

    ch32h4_pin_af(g_pins[_scl].port, g_pins[_scl].bit, _af, CH32H4_CFG_AF_OD_50);
    ch32h4_pin_af(g_pins[_sda].port, g_pins[_sda].bit, _af, CH32H4_CFG_AF_OD_50);

    I2C_TypeDef *dev = ch32h4_i2c_regs(_id);

    I2C_InitTypeDef init = {};
    init.I2C_Mode = I2C_Mode_I2C;
    init.I2C_DutyCycle = I2C_DutyCycle_2;
    /* The address the peripheral matches on is the 7-bit one shifted up: the
     * low bit of the byte on the wire is the read/write direction and not
     * part of the address. Taking the argument unshifted, the way every
     * Arduino core does, means a sketch that says 0x42 answers to 0x42. */
    init.I2C_OwnAddress1 = (uint16_t)(address << 1);
    init.I2C_Ack = I2C_Ack_Enable;
    init.I2C_AcknowledgedAddress = I2C_AcknowledgedAddress_7bit;
    /* A slave does not drive the clock, but CKCFGR still sets the input
     * filtering, and the peripheral will not run with a zero here. */
    init.I2C_ClockSpeed = _clock;
    I2C_Init(dev, &init);

    _rxLen = _rxIndex = 0;
    _txLen = _txIndex = 0;

    ch32h4_i2c_attach_irq(_id, wire_irq_trampoline, this);

    /* EVT for the protocol steps, BUF so a single byte raises RXNE or TXE
     * rather than waiting for BTF, ERR for the acknowledge failure that ends
     * a master read.
     *
     * ERR is not optional. A master reading from this device ends the
     * transfer by NOT acknowledging the last byte, which sets AF -- an error
     * flag raised by the normal, correct end of every read. Left masked, AF
     * stays set and no further address match is ever reported. */
    I2C_ITConfig(dev, I2C_IT_EVT | I2C_IT_BUF | I2C_IT_ERR, ENABLE);
    NVIC_EnableIRQ(ch32h4_i2c_ev_irqn(_id));
    NVIC_EnableIRQ(ch32h4_i2c_er_irqn(_id));

    I2C_Cmd(dev, ENABLE);
    /* After I2C_Cmd, not before. ACK is what makes the part answer its own
     * address at all, and a slave that does not acknowledge is indistinguish-
     * able from one that is not on the bus. */
    I2C_AcknowledgeConfig(dev, ENABLE);

    _running = true;
}

/* One handler, both vectors.
 *
 * Flags come out of STAR1 directly rather than through I2C_CheckEvent(),
 * which tests for an EXACT combination of bits. In slave mode those
 * combinations overlap -- a byte arriving while the previous one is still
 * unread sets RXNE and BTF together -- and an exact match silently does
 * nothing at all for the combination it was not told about.
 */
void TwoWire::handleEvent(bool error) {
    I2C_TypeDef *dev = ch32h4_i2c_regs(_id);
    const uint16_t sr1 = dev->STAR1;

    if (error) {
        if (sr1 & I2C_STAR1_AF) {
            /* The master did not acknowledge the last byte, which is how a
             * master read ENDS. Not a failure: clearing the flag is the whole
             * of the handling. */
            dev->STAR1 = (uint16_t)~I2C_STAR1_AF;
            _txLen = _txIndex = 0;
        }
        if (sr1 & I2C_STAR1_BERR) {
            dev->STAR1 = (uint16_t)~I2C_STAR1_BERR;
        }
        if (sr1 & I2C_STAR1_OVR) {
            /* A byte arrived before the last was read. It is gone, and there
             * is no recovering it; the alternative to clearing the flag is an
             * interrupt that never stops arriving. */
            dev->STAR1 = (uint16_t)~I2C_STAR1_OVR;
        }
        return;
    }

    if (sr1 & I2C_STAR1_ADDR) {
        /* The address matched. Reading STAR1 and then STAR2 is what clears
         * ADDR -- there is no write that does it, and skipping the STAR2 read
         * leaves the peripheral stretching the clock forever. STAR2 also
         * carries TRA, which is the only place the direction of this transfer
         * is stated. */
        const uint16_t sr2 = dev->STAR2;
        const bool transmitting = (sr2 & I2C_STAR2_TRA) != 0;

        if (transmitting) {
            /* A master read. The handler fills _txBuf through write(), so the
             * buffer is cleared first: otherwise it appends to whatever the
             * last transfer left and the master is served stale bytes. */
            _txLen = 0;
            _txIndex = 0;
            if (_onRequest) {
                _onRequest();
            }
            /* Prime the data register here rather than waiting for the TXE
             * interrupt. The master is already clocking, and every cycle
             * before the first byte is written is a cycle of stretched
             * clock. */
            dev->DATAR = (_txIndex < _txLen) ? _txBuf[_txIndex++] : 0xFF;
        } else {
            _rxLen = 0;
            _rxIndex = 0;
        }
        return;
    }

    if (sr1 & I2C_STAR1_STOPF) {
        /* A master write has ended. STOPF clears on a read of STAR1 followed
         * by a WRITE to CTLR1 -- not a read of STAR2, which is what clears
         * ADDR. Setting PE, which is already set, is the way to write CTLR1
         * without disturbing anything. */
        dev->CTLR1 |= I2C_CTLR1_PE;

        if (_rxLen > 0 && _onReceive) {
            _rxIndex = 0;
            _onReceive((int)_rxLen);
        }
        return;
    }

    if (sr1 & I2C_STAR1_RXNE) {
        /* Reading DATAR is what clears RXNE, so this read happens even when
         * the buffer is full: skipping it re-enters the interrupt forever and
         * stretches the clock while it does. */
        const uint8_t b = (uint8_t)dev->DATAR;
        if (_rxLen < BUFFER_LENGTH) {
            _rxBuf[_rxLen++] = b;
        }
        return;
    }

    if (sr1 & I2C_STAR1_TXE) {
        /* Past the end of what the handler supplied, the master is still
         * clocking. It ends the read by not acknowledging, which arrives as
         * AF on the error vector; until then something has to be written or
         * the clock stretches. 0xFF is the idle line, and is at least
         * recognisable as "nothing more". */
        dev->DATAR = (_txIndex < _txLen) ? _txBuf[_txIndex++] : 0xFF;
        return;
    }
}

void TwoWire::end() {
    if (!_running) {
        return;
    }
    I2C_Cmd(ch32h4_i2c_regs(_id), DISABLE);
    _running = false;
}

void TwoWire::setClock(uint32_t freq) {
    _clock = freq;
    if (_running) {
        configure();
    }
}

/* Spin until every bit in `mask` matches `set`, or time out.
 *
 * Every wait here is bounded. An I2C bus with no pull-ups, or a device holding
 * a line, otherwise hangs the sketch forever inside what looks like an
 * ordinary Wire call. */
bool TwoWire::waitFor(uint32_t mask, bool set, uint32_t timeout_us) {
    const uint32_t start = micros();
    I2C_TypeDef *dev = ch32h4_i2c_regs(_id);
    for (;;) {
        const bool now = (dev->STAR1 & mask) != 0;
        if (now == set) {
            return true;
        }
        if ((micros() - start) > timeout_us) {
            return false;
        }
    }
}

void TwoWire::beginTransmission(uint8_t address) {
    _txAddress = address;
    _txLen = 0;
}

uint8_t TwoWire::endTransmission(bool stopBit) {
    if (!_running) {
        return 4;   /* Arduino: 4 = other error */
    }
    I2C_TypeDef *dev = ch32h4_i2c_regs(_id);

    I2C_GenerateSTART(dev, ENABLE);
    if (!waitFor(I2C_STAR1_SB, true, 10000)) {
        I2C_GenerateSTOP(dev, ENABLE);
        return 4;
    }

    I2C_Send7bitAddress(dev, (uint8_t)(_txAddress << 1), I2C_Direction_Transmitter);
    if (!waitFor(I2C_STAR1_ADDR, true, 10000)) {
        /* No ACK for the address: nothing is there. This is the ordinary
         * outcome of a bus scan and must be distinguishable from a bus fault,
         * so it returns 2 rather than 4. */
        I2C_GenerateSTOP(dev, ENABLE);
        dev->STAR1 &= ~I2C_STAR1_AF;
        return 2;
    }
    (void)dev->STAR2;   /* reading STAR1 then STAR2 clears ADDR */

    for (size_t i = 0; i < _txLen; i++) {
        I2C_SendData(dev, _txBuf[i]);
        if (!waitFor(I2C_STAR1_TXE, true, 10000)) {
            I2C_GenerateSTOP(dev, ENABLE);
            return 3;   /* NACK on data */
        }
    }
    waitFor(I2C_STAR1_BTF, true, 10000);

    if (stopBit) {
        I2C_GenerateSTOP(dev, ENABLE);
    }
    _txLen = 0;
    return 0;
}

uint8_t TwoWire::endTransmission(void) {
    return endTransmission(true);
}

size_t TwoWire::requestFrom(uint8_t address, size_t len, bool stopBit) {
    if (!_running || len == 0) {
        return 0;
    }
    if (len > BUFFER_LENGTH) {
        len = BUFFER_LENGTH;
    }
    I2C_TypeDef *dev = ch32h4_i2c_regs(_id);

    _rxLen = 0;
    _rxIndex = 0;

    I2C_AcknowledgeConfig(dev, ENABLE);
    I2C_GenerateSTART(dev, ENABLE);
    if (!waitFor(I2C_STAR1_SB, true, 10000)) {
        I2C_GenerateSTOP(dev, ENABLE);
        return 0;
    }

    I2C_Send7bitAddress(dev, (uint8_t)(address << 1), I2C_Direction_Receiver);
    if (!waitFor(I2C_STAR1_ADDR, true, 10000)) {
        I2C_GenerateSTOP(dev, ENABLE);
        dev->STAR1 &= ~I2C_STAR1_AF;
        return 0;
    }
    (void)dev->STAR2;

    for (size_t i = 0; i < len; i++) {
        if (i + 1 == len) {
            /* NACK the last byte before reading it: the peripheral decides
             * what to send on the ninth clock as that byte arrives, so setting
             * it afterwards is too late and the device sends one more. */
            I2C_AcknowledgeConfig(dev, DISABLE);
            if (stopBit) {
                I2C_GenerateSTOP(dev, ENABLE);
            }
        }
        if (!waitFor(I2C_STAR1_RXNE, true, 10000)) {
            break;
        }
        _rxBuf[_rxLen++] = I2C_ReceiveData(dev);
    }

    I2C_AcknowledgeConfig(dev, ENABLE);
    return _rxLen;
}

size_t TwoWire::requestFrom(uint8_t address, size_t len) {
    return requestFrom(address, len, true);
}

size_t TwoWire::write(uint8_t data) {
    if (_txLen >= BUFFER_LENGTH) {
        return 0;
    }
    _txBuf[_txLen++] = data;
    return 1;
}

size_t TwoWire::write(const uint8_t *data, size_t len) {
    size_t n = 0;
    while (n < len && write(data[n])) {
        n++;
    }
    return n;
}

int TwoWire::available() {
    return (int)(_rxLen - _rxIndex);
}

int TwoWire::read() {
    if (_rxIndex >= _rxLen) {
        return -1;
    }
    return _rxBuf[_rxIndex++];
}

int TwoWire::peek() {
    if (_rxIndex >= _rxLen) {
        return -1;
    }
    return _rxBuf[_rxIndex];
}

void TwoWire::flush() {
    _rxLen = _rxIndex = 0;
    _txLen = 0;
}

/* Both run in interrupt context, and onRequest() runs with the master
 * already clocking: it must fill the buffer with write() and return. Anything
 * that waits there stretches the bus clock for as long as it takes. */
void TwoWire::onReceive(void (*cb)(int)) { _onReceive = cb; }
void TwoWire::onRequest(void (*cb)(void)) { _onRequest = cb; }

