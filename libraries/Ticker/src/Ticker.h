/* Periodic and one-shot callbacks, as in arduino-pico and the ESP cores.
 *
 * Driven from the millisecond tick rather than from a hardware timer. This
 * part has twelve timers and they are genuinely contended -- analogWrite,
 * Servo, tone and I2S all want one -- so spending one on a software timer that
 * does not need the precision would be a poor trade. A Ticker is accurate to a
 * millisecond and costs nothing but a table entry.
 *
 * The callbacks run from loop(), not from an interrupt. That is deliberate: a
 * callback that can call Serial.print() or malloc() is far more useful than
 * one that cannot, and a sketch that needs interrupt latency should use
 * attachInterrupt or take a timer directly. It does mean a Ticker cannot fire
 * while loop() is blocked -- update() is called from yield(), so a delay()
 * still services them.
 */
#pragma once

#include <Arduino.h>

class Ticker {
public:
    typedef void (*callback_t)(void);
    typedef void (*callback_arg_t)(void *);

    Ticker();
    ~Ticker();

    /* Repeat every `ms` until detach(). */
    void attach_ms(uint32_t ms, callback_t cb);
    void attach_ms(uint32_t ms, callback_arg_t cb, void *arg);

    /* Seconds, for the ESP-compatible spelling. */
    void attach(float seconds, callback_t cb);

    /* Fire once, then detach itself. */
    void once_ms(uint32_t ms, callback_t cb);
    void once(float seconds, callback_t cb);

    void detach();
    bool active() const { return _active; }

    /* Runs every due callback. Called from yield(), so a sketch does not have
     * to remember -- but a sketch that spins without yielding will not see its
     * tickers fire, which is the one thing to know about them. */
    static void update();

private:
    void arm(uint32_t ms, callback_t cb, callback_arg_t cbArg, void *arg, bool repeat);

    bool _active = false;
    bool _repeat = false;
    uint32_t _interval = 0;
    uint32_t _next = 0;
    callback_t _cb = nullptr;
    callback_arg_t _cbArg = nullptr;
    void *_arg = nullptr;

    Ticker *_nextTicker = nullptr;
    static Ticker *_head;
};
