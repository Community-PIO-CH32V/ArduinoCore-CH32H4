/* Hardware SPI for the CH32H41x.
 *
 * Four peripherals, and which one you get is decided by the pins: the variant
 * carries the alternate-function map and SPIClass finds the peripheral that
 * can serve all three signals at once. Searching per-signal and hoping they
 * agree is how you end up clocking from SPI1 and shifting data out of SPI3,
 * which produces a clock, no data, and something that looks like a wiring
 * fault.
 *
 * SPI1 is on HB2; SPI2, SPI3 and SPI4 are on HB1. The split runs the opposite
 * way from I2C. Enabling the wrong bus gives a peripheral whose registers read
 * back as zeroes with no error whatsoever, so the bus is never named at a call
 * site -- ch32h4_spi_clock_enable() owns it.
 */
#pragma once

#include <Arduino.h>
#include "api/HardwareSPI.h"

/* Named SPIClassCH32H4, not SPIClass.
 *
 * ArduinoCore-API already has `typedef HardwareSPI SPIClass` in namespace
 * arduino, which the core makes visible with `using namespace arduino`. A
 * second global SPIClass would be ambiguous with it, not an override of it.
 *
 * Nothing is lost: `SPIClass` still names arduino::HardwareSPI, this derives
 * from it, so a library holding an `SPIClass *` can point at SPI and call
 * through it. */
class SPIClassCH32H4 : public arduino::HardwareSPI {
public:
    /* Defaults to the board's SPI1 pins: PA5 SCK, PA6 MISO, PA7 MOSI. Those
     * are also the only SPI pins on the 3.3 V rail. */
    SPIClassCH32H4(pin_size_t sck = PIN_SPI_SCK,
             pin_size_t miso = PIN_SPI_MISO,
             pin_size_t mosi = PIN_SPI_MOSI);

    void begin() override;
    void end() override;

    uint8_t transfer(uint8_t data) override;
    uint16_t transfer16(uint16_t data) override;
    void transfer(void *buf, size_t count) override;

    /* Separate buffers, which a DMA transfer can do without the caller
     * copying first. `rx` may be null to send without keeping what comes
     * back. */
    void transfer(const void *tx, void *rx, size_t count);

    /* Block transfers above this many bytes go through DMA; shorter ones stay
     * on the polled path, where setting up two channels costs more than it
     * saves. Exposed so a test can prove both paths rather than whichever the
     * threshold happens to select. */
    static const size_t DMA_THRESHOLD = 32;

    void beginTransaction(arduino::SPISettings settings) override;
    void endTransaction() override;

    void usingInterrupt(int interruptNumber) override;
    void notUsingInterrupt(int interruptNumber) override;
    void attachInterrupt() override;
    void detachInterrupt() override;

    /* Change the pins before begin(). Returns false if no single peripheral
     * can serve all three, which is the failure worth reporting -- the pins
     * are individually valid and the combination is not. */
    bool setSCK(pin_size_t pin);
    bool setMISO(pin_size_t pin);
    bool setMOSI(pin_size_t pin);

    /* Which peripheral these pins resolved to, 1-4, or 0 if none can. */
    uint8_t peripheral() const { return _id; }

private:
    void applySettings(const arduino::SPISettings &settings);

    /* Either buffer may be null: null tx sends 0xFF, null rx discards. */
    void transferPolled(const uint8_t *tx, uint8_t *rx, size_t count);
    bool transferDMA(const uint8_t *tx, uint8_t *rx, size_t count);

    pin_size_t _sck, _miso, _mosi;
    uint8_t _id = 0;
    bool _running = false;
    bool _inTransaction = false;
    uint32_t _clock = 4000000;
    uint8_t _bitOrder = MSBFIRST;
    uint8_t _dataMode = SPI_MODE0;
};

extern SPIClassCH32H4 SPI;
