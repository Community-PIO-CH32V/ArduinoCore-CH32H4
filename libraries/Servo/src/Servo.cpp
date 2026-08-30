#include "Servo.h"
#include "ch32h4_pinmap.h"
#include "ch32h4_timer.h"

/* One timer tick per microsecond, so a pulse width in microseconds is written
 * straight into the compare register with no arithmetic to get wrong. At a
 * 20000-tick period that is a 50 Hz frame, which is what an RC servo wants. */
#define SERVO_TICK_HZ  1000000u

Servo::Servo() { }

uint8_t Servo::attach(int pin) {
    return attach(pin, MIN_PULSE_WIDTH, MAX_PULSE_WIDTH);
}

uint8_t Servo::attach(int pin, int min, int max) {
    if (pin < 0 || pin >= PINS_COUNT) {
        return INVALID_SERVO;
    }
    _min = min;
    _max = max;

    /* Find a timer this pin can use that Servo can actually have.
     *
     * ch32h4_pwm_find() prefers a timer PWM already owns, which is exactly
     * wrong here -- joining analogWrite's timer would drag the servo to its
     * ~1 kHz frame. So the map is walked directly and only a free timer, or
     * one Servo already holds, is accepted. */
    uint8_t chosen_timer = 0, chosen_channel = 0, chosen_af = 0;

    for (size_t i = 0; i < g_pwm_af_map_len; i++) {
        const ch32h4_pwm_af_t *e = &g_pwm_af_map[i];
        if (e->pin != (uint8_t)pin || e->negated) {
            continue;
        }
        const uint8_t owner = ch32h4_timer_owner(e->timer);
        if (owner == CH32H4_TIMER_FREE || owner == CH32H4_TIMER_SERVO) {
            chosen_timer = e->timer;
            chosen_channel = e->channel;
            chosen_af = e->af;
            break;
        }
    }

    if (chosen_timer == 0) {
        return INVALID_SERVO;
    }
    if (!ch32h4_timer_claim(chosen_timer, CH32H4_TIMER_SERVO)) {
        return INVALID_SERVO;
    }

    _pin = (pin_size_t)pin;
    _timer = chosen_timer;
    _channel = chosen_channel;
    _af = chosen_af;

    ch32h4_timer_clock_enable(_timer);

    TIM_TypeDef *dev = ch32h4_timer_dev(_timer);

    /* Only reset the block for the FIRST servo on this timer. A second servo
     * on another channel of the same timer must not wipe the first one's
     * compare register, and re-initialising the time base would do exactly
     * that. The registry is what tells the two apart: if we already owned the
     * timer before this claim, someone is using it. */
    static uint16_t configured_timers = 0;
    const bool first = (configured_timers & (1u << _timer)) == 0;
    if (first) {
        ch32h4_timer_reset(_timer);

        /* Timers divide HCLK, never SystemCoreClock. */
        const uint32_t prescaler = ch32h4_timer_input_clock(_timer) / SERVO_TICK_HZ;

        TIM_TimeBaseInitTypeDef t = {};
        t.TIM_Prescaler = (uint16_t)(prescaler - 1u);
        t.TIM_CounterMode = TIM_CounterMode_Up;
        t.TIM_Period = (uint16_t)(REFRESH_INTERVAL - 1u);
        t.TIM_ClockDivision = TIM_CKD_DIV1;
        t.TIM_RepetitionCounter = 0;
        TIM_TimeBaseInit(dev, &t);
        TIM_ARRPreloadConfig(dev, ENABLE);

        configured_timers |= (uint16_t)(1u << _timer);
    }

    ch32h4_pin_af(g_pins[_pin].port, g_pins[_pin].bit, _af, CH32H4_CFG_AF_PP_50);

    TIM_OCInitTypeDef oc = {};
    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_Pulse = (uint16_t)_pulse;
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    oc.TIM_OCIdleState = TIM_OCIdleState_Reset;

    switch (_channel) {
        case 1: TIM_OC1Init(dev, &oc); TIM_OC1PreloadConfig(dev, TIM_OCPreload_Enable); break;
        case 2: TIM_OC2Init(dev, &oc); TIM_OC2PreloadConfig(dev, TIM_OCPreload_Enable); break;
        case 3: TIM_OC3Init(dev, &oc); TIM_OC3PreloadConfig(dev, TIM_OCPreload_Enable); break;
        default: TIM_OC4Init(dev, &oc); TIM_OC4PreloadConfig(dev, TIM_OCPreload_Enable); break;
    }

    /* TIM1 and TIM8 are advanced-control timers: their outputs stay
     * electrically disabled until MOE is set, and nothing says so. The others
     * ignore this. */
    if (_timer == 1 || _timer == 8) {
        TIM_CtrlPWMOutputs(dev, ENABLE);
    }

    TIM_Cmd(dev, ENABLE);
    writeMicroseconds(_pulse);
    return _channel;
}

void Servo::detach() {
    if (!attached()) {
        return;
    }
    /* Leave the pin as an input rather than parked at whatever level the last
     * pulse ended on -- a servo held at a live level keeps drawing current
     * trying to hold position. */
    pinMode(_pin, INPUT);
    ch32h4_timer_release(_timer, CH32H4_TIMER_SERVO);
    _timer = 0;
    _pin = (pin_size_t)-1;
}

bool Servo::attached() {
    return _timer != 0;
}

void Servo::write(int value) {
    /* Arduino's overload: below 180 it is an angle, above it a pulse width.
     * The ambiguity is historical and sketches depend on it. */
    if (value < 0) {
        value = 0;
    }
    if (value <= 180) {
        value = map(value, 0, 180, _min, _max);
    }
    writeMicroseconds(value);
}

void Servo::writeMicroseconds(int value) {
    if (value < _min) {
        value = _min;
    } else if (value > _max) {
        value = _max;
    }
    _pulse = value;

    if (!attached()) {
        return;
    }
    TIM_TypeDef *dev = ch32h4_timer_dev(_timer);
    switch (_channel) {
        case 1: TIM_SetCompare1(dev, (uint16_t)_pulse); break;
        case 2: TIM_SetCompare2(dev, (uint16_t)_pulse); break;
        case 3: TIM_SetCompare3(dev, (uint16_t)_pulse); break;
        default: TIM_SetCompare4(dev, (uint16_t)_pulse); break;
    }
}

int Servo::read() {
    return map(_pulse, _min, _max, 0, 180);
}

int Servo::readMicroseconds() {
    return _pulse;
}
