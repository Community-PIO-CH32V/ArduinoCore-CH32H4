/* RC servos.
 *
 * Hardware PWM rather than a bit-banged pulse train: this part has ten timers
 * with four channels each, so there is no reason to burn CPU on it.
 *
 * The interesting part is arbitration. A servo needs a 20 ms frame, and the
 * period register is shared by all four channels of a timer -- so a servo
 * cannot share a timer with analogWrite(), which wants about 1 kHz. Nothing in
 * the hardware stops that: the second caller silently reprograms the period
 * and the first one's output changes frequency underneath it. A servo
 * twitching at 1 kHz because analogWrite() got there first is a miserable
 * thing to debug.
 *
 * So Servo claims its timer from the core's registry (ch32h4_timer.h). If
 * something else holds it, attach() looks for another timer the pin can use,
 * and fails honestly if there is none rather than producing a broken output.
 */
#pragma once

#include <Arduino.h>

#define MIN_PULSE_WIDTH       544    /* us, for a 0-degree angle */
#define MAX_PULSE_WIDTH      2400    /* us, for 180 degrees */
#define DEFAULT_PULSE_WIDTH  1500
#define REFRESH_INTERVAL    20000    /* us between frames: 50 Hz */

#define INVALID_SERVO         255

class Servo {
public:
    Servo();

    /* Returns the channel index, or INVALID_SERVO if no timer could be had --
     * either the pin has no timer channel at all, or every timer it could use
     * is held by PWM, tone or I2S. Check it: the failure is silent otherwise,
     * and a servo that never moves looks like a wiring fault. */
    uint8_t attach(int pin);
    uint8_t attach(int pin, int min, int max);

    void detach();
    void write(int value);             /* angle 0-180, or a pulse width in us */
    void writeMicroseconds(int value);
    int read();                        /* the angle last written */
    int readMicroseconds();
    bool attached();

    /* Which timer this servo took, 1-12, or 0. Exposed so a sketch -- or a
     * test -- can see the arbitration actually happened. */
    uint8_t timer() const { return _timer; }

private:
    pin_size_t _pin = (pin_size_t)-1;
    uint8_t _timer = 0;
    uint8_t _channel = 0;
    uint8_t _af = 0;
    int _min = MIN_PULSE_WIDTH;
    int _max = MAX_PULSE_WIDTH;
    int _pulse = DEFAULT_PULSE_WIDTH;
};
