#pragma once

#include "api/HardwareSerial.h"
#include "ch32h417.h"
#include "ch32h4_pinmap.h"

/* One class for all eight USARTs; one object per port, each in its own
 * translation unit.
 *
 * THE SEPARATE FILES ARE THE POINT. An object defined here would be linked
 * into every sketch, so eight ports would cost eight 256-byte receive buffers,
 * eight static constructors and their code, whether or not a sketch says
 * "Serial3". Each port lives in HardwareSerialN.cpp instead, so the archive
 * member is pulled in only when something references SerialN -- which is also
 * why the core is no longer linked with --whole-archive. See the EXTERN()
 * block in the variant's linker script for what that forced back into being
 * explicit.
 *
 * USART1 is special in one way only: the boot console owns it, so Serial1
 * shares the inter-core lock with ch32h4_console_puts(). The other seven have
 * no such sharing and take no lock.
 *
 * PINS. Every port has a default pair, and every port can be moved before
 * begin() with setTX()/setRX() -- this silicon multiplexes like an STM32F4
 * rather than remapping like a CH32V307, so a different pin is a different AF
 * number and not a remap bit. begin() refuses a pin that cannot carry the
 * signal for that peripheral, because the alternative is a port that reports
 * TXE and TC set forever and puts nothing on the wire.
 */
class CH32H4Serial : public arduino::HardwareSerial {
public:
    /* `id` is 1-8. The pins are defaults; setTX()/setRX() override them until
     * begin() is called. */
    CH32H4Serial(uint8_t id, pin_size_t tx, pin_size_t rx)
        : _id(id), _tx(tx), _rx_pin(rx) { }

    void begin(unsigned long baud) override { begin(baud, SERIAL_8N1); }
    void begin(unsigned long baud, uint16_t config) override;
    void end() override;

    int available() override;
    int peek() override;
    int read() override;
    void flush() override;
    size_t write(uint8_t c) override;
    /* Overridden, not inherited from Print's byte loop: the whole buffer goes
     * out under one console lock, so a print() from this core cannot be cut in
     * half by the other core printing through ch32h4_console_puts(). */
    size_t write(const uint8_t *buf, size_t n) override;
    using Print::write;

    operator bool() override { return _running; }

    /* Before begin(). False if the pin cannot carry that signal for this
     * USART, in which case the previous pin is kept. */
    bool setTX(pin_size_t pin);
    bool setRX(pin_size_t pin);

    /* Which USART this is, 1-8. */
    uint8_t id() const { return _id; }

    /* Called from USARTn_IRQHandler. Public because the ISR is extern "C". */
    void _isr();

private:
    uint8_t _id;
    pin_size_t _tx;
    pin_size_t _rx_pin;
    USART_TypeDef *_usart = nullptr;
    bool _running = false;

    /* A power of two, so the wrap is a mask rather than a modulo. */
    static const uint16_t RX_SIZE = 256;
    volatile uint16_t _head = 0;
    volatile uint16_t _tail = 0;
    uint8_t _rx[RX_SIZE];
};

/* Declared for all eight; defined one per file, so referencing one is what
 * links it. Serial1 is USART1 on PA9/PA10, into the WCH-Link's VCP, and is
 * what `Serial` maps to when the console is not USB CDC. */
extern CH32H4Serial Serial1;
extern CH32H4Serial Serial2;
extern CH32H4Serial Serial3;
extern CH32H4Serial Serial4;
extern CH32H4Serial Serial5;
extern CH32H4Serial Serial6;
extern CH32H4Serial Serial7;
extern CH32H4Serial Serial8;
