/* I2C for the CH32H41x.
 *
 * Four peripherals; which one you get is decided by the SCL/SDA pair, from the
 * map in the variant. The pair matters: the silicon pairs the pads, and an SCL
 * from one row with an SDA from another is not a configuration the hardware
 * offers.
 *
 * I2C1, I2C2 and I2C3 are on HB1; only I2C4 is on HB2. That is the opposite
 * split from SPI, where SPI1 is on HB2 and 2-4 are on HB1, and enabling the
 * wrong bus gives a peripheral whose registers read back as zeroes with no
 * error at all.
 *
 * THERE ARE NO INTERNAL PULL-UPS. The F1-style open-drain encoding on this
 * part does not offer them, so both lines need real resistors. A bus with none
 * reads as permanently busy, which this library reports as such rather than as
 * a missing device -- see recover().
 */
#pragma once

#include <Arduino.h>
#include "api/HardwareI2C.h"

class TwoWire : public arduino::HardwareI2C {
public:
    TwoWire(pin_size_t scl = PIN_WIRE_SCL, pin_size_t sda = PIN_WIRE_SDA);

    void begin() override;
    void begin(uint8_t address) override;
    void end() override;

    void setClock(uint32_t freq) override;

    void beginTransmission(uint8_t address) override;
    uint8_t endTransmission(bool stopBit) override;
    uint8_t endTransmission(void) override;

    size_t requestFrom(uint8_t address, size_t len, bool stopBit) override;
    size_t requestFrom(uint8_t address, size_t len) override;

    void onReceive(void (*)(int)) override;
    void onRequest(void (*)(void)) override;

    size_t write(uint8_t data) override;
    size_t write(const uint8_t *data, size_t len) override;
    int available() override;
    int read() override;
    int peek() override;
    void flush() override;
    using Print::write;

    /* Change the pins before begin(). False if the pair is not one the silicon
     * offers, which is worth distinguishing from "that pin does not exist". */
    bool setSCL(pin_size_t pin);
    bool setSDA(pin_size_t pin);

    /* Which peripheral the pins resolved to, 1-4, or 0 if none can serve them. */
    uint8_t peripheral() const { return _id; }

    /* Unwedge a bus a device is holding low.
     *
     * A stuck bus CANNOT be cleared by any register write -- the reference
     * manual says so and it is true. The only way out is to take the pins away
     * from the peripheral, clock SCL by hand nine times (a full byte plus the
     * ACK, which is enough for any device to finish whatever transfer it
     * thinks is in progress), reset the peripheral, and re-initialise.
     *
     * Returns true if the bus came back. begin() calls this automatically when
     * it finds the bus already busy, which is the common case after a reset
     * that interrupted a transfer -- the peripheral's BUSY latch survives a
     * warm reset, so without it a re-flash mid-transfer bricks the bus until
     * a power cycle. */
    bool recover();

private:
    bool waitFor(uint32_t mask, bool set, uint32_t timeout_us);
    void configure();

    pin_size_t _scl, _sda;
    uint8_t _id = 0;
    uint8_t _af = 0;
    bool _running = false;
    uint32_t _clock = 100000;

    uint8_t _txAddress = 0;
    static const size_t BUFFER_LENGTH = 128;
    uint8_t _txBuf[BUFFER_LENGTH];
    size_t _txLen = 0;
    uint8_t _rxBuf[BUFFER_LENGTH];
    size_t _rxLen = 0, _rxIndex = 0;
};

extern TwoWire Wire;
