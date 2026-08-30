#include "Wire.h"

/* I2C1-3 are on HB1; only I2C4 is on HB2. The opposite split from SPI. */
static void i2c_clock_enable(uint8_t id) {
    if (id == 4) {
        ch32h4_clock_enable(CH32_BUS_HB2, RCC_HB2Periph_I2C4);
        return;
    }
    static const uint32_t hb1[3] = {
        RCC_HB1Periph_I2C1, RCC_HB1Periph_I2C2, RCC_HB1Periph_I2C3,
    };
    ch32h4_clock_enable(CH32_BUS_HB1, hb1[id - 1]);
}

static void i2c_reset(uint8_t id) {
    if (id == 4) {
        ch32h4_block_reset(CH32_BUS_HB2, RCC_HB2Periph_I2C4);
        return;
    }
    static const uint32_t hb1[3] = {
        RCC_HB1Periph_I2C1, RCC_HB1Periph_I2C2, RCC_HB1Periph_I2C3,
    };
    ch32h4_block_reset(CH32_BUS_HB1, hb1[id - 1]);
}

static I2C_TypeDef *i2c_regs(uint8_t id) {
    switch (id) {
        case 1: return I2C1;
        case 2: return I2C2;
        case 3: return I2C3;
        default: return I2C4;
    }
}

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
    i2c_reset(_id);
    configure();

    return freed;
}

void TwoWire::configure() {
    i2c_clock_enable(_id);

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

    I2C_Init(i2c_regs(_id), &init);
    I2C_Cmd(i2c_regs(_id), ENABLE);
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
    i2c_reset(_id);
    configure();
    _running = true;

    if (I2C_GetFlagStatus(i2c_regs(_id), I2C_FLAG_BUSY) != RESET) {
        recover();
    }
}

void TwoWire::begin(uint8_t address) {
    /* Slave mode is not implemented yet. Starting as a master when a sketch
     * asked to be a slave would look like it worked and then never answer, so
     * refuse instead: peripheral() stays 0. */
    (void)address;
}

void TwoWire::end() {
    if (!_running) {
        return;
    }
    I2C_Cmd(i2c_regs(_id), DISABLE);
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
    I2C_TypeDef *dev = i2c_regs(_id);
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
    I2C_TypeDef *dev = i2c_regs(_id);

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
    I2C_TypeDef *dev = i2c_regs(_id);

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

/* Slave-mode callbacks. Accepted and ignored until slave mode exists; see
 * begin(uint8_t). */
void TwoWire::onReceive(void (*cb)(int)) { (void)cb; }
void TwoWire::onRequest(void (*cb)(void)) { (void)cb; }

TwoWire Wire;
