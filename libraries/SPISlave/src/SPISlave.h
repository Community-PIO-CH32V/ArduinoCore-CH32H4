/* SPI in slave mode for the CH32H41x.
 *
 * The API is arduino-pico's SPISlave, so a sketch written against that core
 * moves over unchanged: setData() queues what the next frame will shift out,
 * onDataRecv() reports what came in, onDataSent() says the queued data has
 * gone.
 *
 * WHAT A SLAVE CANNOT DO. It does not decide when a transfer happens or how
 * fast; the master's clock does both. That has two consequences worth stating
 * because neither is obvious from the API:
 *
 *   - setData() must be called BEFORE the master starts clocking. Data queued
 *     halfway through a frame goes out in the next one. There is nothing this
 *     library can do about that -- the first byte has to be sitting in the
 *     shift register when the first edge arrives.
 *
 *   - The clock in SPISettings is ignored. It is accepted because the pico
 *     API takes it and dropping the argument would break source
 *     compatibility, but a slave has no baud divider. Bit order and mode are
 *     honoured and MUST match the master, since a slave has no way to detect
 *     that they do not: a mode mismatch shifts data by half a bit and shows
 *     up as consistently wrong bytes rather than as an error.
 *
 * CHIP SELECT. With a CS pin configured, the peripheral uses hardware NSS and
 * a frame ends when CS rises -- which is what makes onDataRecv() meaningful,
 * and what re-synchronises the shift register if a clock edge is ever missed.
 * Without one the slave is permanently selected, stays byte-aligned only as
 * long as nothing glitches, and reports a frame when the receive buffer
 * reaches the length that was queued. Use a CS pin if you have one.
 */
#pragma once

#include <Arduino.h>
#include "api/HardwareSPI.h"

/* Called from interrupt context, both of them. Keep them short, do not call
 * anything that blocks, and mark anything they share with loop() volatile. */
typedef void (*spi_slave_recv_cb)(uint8_t *data, size_t len);
typedef void (*spi_slave_send_cb)(void);

class SPISlaveClass {
public:
    /* Defaults to SPI4 on PE2/PE5/PE6 with NSS on PE4. Those four pins are on
     * the 3.3 V rail and none of them is spoken for elsewhere on this board,
     * which makes SPI4 the natural partner for the SPI1 master. */
    SPISlaveClass(pin_size_t sck = PIN_SPI_SLAVE_SCK,
                  pin_size_t miso = PIN_SPI_SLAVE_MISO,
                  pin_size_t mosi = PIN_SPI_SLAVE_MOSI,
                  pin_size_t cs = PIN_SPI_SLAVE_CS);

    /* All four take effect at the next begin(); changing pins on a running
     * slave would drop a frame in progress, so they refuse while running. */
    bool setSCK(pin_size_t pin);
    bool setMISO(pin_size_t pin);
    bool setMOSI(pin_size_t pin);
    bool setCS(pin_size_t pin);

    /* arduino-pico's names for the same two data pins, from the slave's point
     * of view: RX is the line the master drives (MOSI), TX the one this
     * device drives (MISO). */
    bool setRX(pin_size_t pin) { return setMOSI(pin); }
    bool setTX(pin_size_t pin) { return setMISO(pin); }

    void onDataRecv(spi_slave_recv_cb cb) { _recvCb = cb; }
    void onDataSent(spi_slave_send_cb cb) { _sentCb = cb; }

    /* False if the pins do not all land on one peripheral. */
    bool begin(arduino::SPISettings settings);
    bool begin() { return begin(arduino::SPISettings()); }
    bool end();

    /* Queue the bytes the next frame will shift out. Returns how many were
     * accepted, which is fewer than asked for if `len` exceeds BUFFER_LENGTH.
     * Replaces anything still queued rather than appending: a slave that
     * silently sends stale data from two frames ago is worse than one that
     * sends the newest thing it was given. */
    size_t setData(const uint8_t *data, size_t len);

    /* Which peripheral the pins resolved to, 1-4, or 0. */
    uint8_t peripheral() const { return _id; }

    /* Whether hardware NSS is in use. False means the CS pin was not given,
     * or is not one this part is known to be able to use as NSS -- see
     * g_spi_nss_map in the variant. Worth checking in a sketch that assumed
     * it had chip select. */
    bool hardwareCS() const { return _hardCS; }

    static const size_t BUFFER_LENGTH = 256;

    /* Not for callers: the interrupt path. Public because the C dispatch in
     * the core has to reach it. */
    void handleIRQ();
    void handleCSRise();

private:
    bool resolve();

    pin_size_t _sck, _miso, _mosi, _cs;
    uint8_t _id = 0;
    bool _running = false;
    bool _hardCS = false;
    uint8_t _bitOrder = MSBFIRST;
    uint8_t _dataMode = SPI_MODE0;

    spi_slave_recv_cb _recvCb = nullptr;
    spi_slave_send_cb _sentCb = nullptr;

    volatile uint8_t _txBuf[BUFFER_LENGTH];
    volatile size_t _txLen = 0;
    volatile size_t _txIndex = 0;
    volatile bool _sentFired = true;

    uint8_t _rxBuf[BUFFER_LENGTH];
    volatile size_t _rxLen = 0;
};

extern SPISlaveClass SPISlave;
