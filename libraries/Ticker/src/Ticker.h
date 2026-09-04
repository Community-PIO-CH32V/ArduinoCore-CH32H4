/* Periodic and one-shot callbacks, as in arduino-pico and the ESP cores.
 *
 * Driven by a HARDWARE TIMER. A Ticker fires whether or not the sketch is in
 * loop(), whether or not it yields, and whether or not it is blocked inside
 * somebody's library. That is the entire point of the class, and an earlier
 * version of this file got it wrong: it ticked from yield(), which meant a
 * sketch spinning in a `while` saw no callbacks at all and the timing of every
 * other sketch depended on what loop() happened to be doing.
 *
 * ## Which timer, and what it costs
 *
 * TIM4. Chosen by counting, not by taste: for each timer, how many pins would
 * lose PWM entirely if it were dedicated here --
 *
 *     TIM4     12 pins reachable      0 pins lose PWM
 *     TIM8      8                     0     (advanced timer; Servo and PWM
 *                                            need its complementary outputs)
 *     TIM3     13                     2     PB4 PB5
 *     TIM9     14                     5
 *     TIM1      8                     6
 *
 * Every pin TIM4 can drive has another timer that can drive it too, so
 * analogWrite() loses nothing at all -- ch32h4_pwm_find() simply takes the
 * next option. TIM8 is equally free but is one of only two timers with
 * complementary outputs, which Servo and motor-style PWM actually need.
 *
 * TIM4 is claimed through the core's timer registry, the same one tone(),
 * Servo, I2S and ADCInput contend through, and only while at least one Ticker
 * is attached. A sketch that never uses Ticker leaves TIM4 free for
 * analogWrite; the first attach() takes it, the last detach() gives it back.
 * If something else already holds it, attach() fails rather than quietly
 * reprogramming another subsystem's period.
 *
 * ## Callbacks run in interrupt context
 *
 * This is the trade for not needing update(). A callback must not block, must
 * not delay(), and should treat anything it touches as shared with loop() --
 * `volatile`, or guarded by noInterrupts(). Serial.print() from a callback
 * will usually work and is still a bad idea: it can block for as long as the
 * UART needs.
 *
 * Set a flag and do the work in loop() if the work is not trivial. That is the
 * same advice every ESP and RP2040 core gives for the same reason.
 */
#pragma once

#include <Arduino.h>

class Ticker {
public:
    typedef void (*callback_t)(void);
    typedef void (*callback_arg_t)(void *);

    Ticker();
    ~Ticker();

    /* Repeat every `ms` until detach(). False if TIM4 could not be had, or if
     * `ms` is zero. */
    bool attach_ms(uint32_t ms, callback_t cb);
    bool attach_ms(uint32_t ms, callback_arg_t cb, void *arg);

    /* Seconds, for the ESP-compatible spelling. */
    bool attach(float seconds, callback_t cb);

    /* Fire once, then detach itself. */
    bool once_ms(uint32_t ms, callback_t cb);
    bool once(float seconds, callback_t cb);

    void detach();
    bool active() const { return _active; }

    /* Kept so that sketches written against the old software Ticker still
     * compile. It does nothing: the timer fires the callbacks now, and calling
     * this neither helps nor is required. */
    static void update() {}

    /* The timer this class uses, for a sketch that wants to know what it is
     * competing with -- ch32h4_timer_owner(Ticker::timerId()) says whether it
     * is free. */
    static uint8_t timerId();

    /* Called by the TIM4 update interrupt, through the core's timer dispatch.
     * Public because a free function cannot reach the list, and not something
     * a sketch has any reason to call. */
    static void serviceFromIsr();

private:
    bool arm(uint32_t ms, callback_t cb, callback_arg_t cbArg, void *arg,
             bool repeat);

    /* Written by the ISR, read by everything else. */
    volatile bool _active = false;
    bool _repeat = false;
    uint32_t _interval = 0;
    volatile uint32_t _remaining = 0;
    callback_t _cb = nullptr;
    callback_arg_t _cbArg = nullptr;
    void *_arg = nullptr;

    Ticker *_nextTicker = nullptr;
    static Ticker *_head;
};
