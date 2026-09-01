#pragma once

#include "api/HardwareSerial.h"
#include "ch32h417.h"

/* Serial is USART1 on PA9/PA10 (AF7), into the WCH-Link's VCP.
 *
 * `Serial` is the USB CDC device by default and this object is `Serial1`; a
 * build option swaps them. Keeping the swap matters: a fault during static
 * initialisation happens before USB has enumerated, so a board that only
 * speaks CDC cannot report one, and the porting notes for this silicon are
 * emphatic that silence is the worst diagnostic there is.
 */
class CH32H4Serial : public arduino::HardwareSerial {
public:
    explicit CH32H4Serial(USART_TypeDef *usart) : _usart(usart) { }

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

    /* Called from USART1_IRQHandler. Public because the ISR is extern "C". */
    void _isr();

private:
    USART_TypeDef *_usart;
    bool _running = false;

    /* A power of two, so the wrap is a mask rather than a modulo. */
    static const uint16_t RX_SIZE = 256;
    volatile uint16_t _head = 0;
    volatile uint16_t _tail = 0;
    uint8_t _rx[RX_SIZE];
};

/* USART1 on PA9/PA10, into the WCH-Link's VCP. Always Serial1, whatever
 * `Serial` is bound to -- see Arduino.h. */
extern CH32H4Serial Serial1;
