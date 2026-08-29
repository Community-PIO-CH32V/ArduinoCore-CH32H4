#pragma once

#include "api/HardwareSerial.h"

/* Serial over USB CDC.
 *
 * This is `Serial` by default. `Serial1` is USART1 on PA9/PA10, and a build
 * option swaps them -- see boards.txt. The swap is worth having: a fault
 * during static initialisation happens before USB has enumerated, so a board
 * that only speaks over CDC cannot report one.
 */
class CH32H4SerialUSB : public arduino::HardwareSerial {
public:
    void begin(unsigned long baud) override { begin(baud, SERIAL_8N1); }
    void begin(unsigned long baud, uint16_t config) override;
    void end() override;

    int available() override;
    int peek() override;
    int read() override;
    void flush() override;
    size_t write(uint8_t c) override;
    size_t write(const uint8_t *buffer, size_t size) override;
    using Print::write;

    /* True once the host has opened the port. A sketch that waits for this
     * before printing gets its first lines; one that does not will lose
     * whatever it wrote before enumeration, which is normal CDC behaviour and
     * why `while (!Serial) {}` is the usual idiom. */
    operator bool() override;

    /* The line coding the host asked for. Meaningless to a CDC device on its
     * own, but sketches use the magic 1200-baud touch to request a reset, and
     * some tools read it back. */
    uint32_t baud();

private:
    bool _running = false;
};

extern CH32H4SerialUSB SerialUSB;
