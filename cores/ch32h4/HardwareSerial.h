#pragma once

#include "api/HardwareSerial.h"
#include "ch32h417.h"

/* Serial is USART1 on PA9/PA10 (AF7), into the WCH-Link's VCP.
 *
 * From M2 onward `Serial` becomes the USB CDC device and this object moves to
 * `Serial1`, with a build option to swap them back -- the porting notes are
 * emphatic that a console which exists before static constructors run is worth
 * keeping, because a fault during static init is otherwise completely silent.
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

extern CH32H4Serial Serial;
extern CH32H4Serial &Serial1;
