/* tone() and noTone().
 *
 * Hardware PWM at 50% duty, which is what a piezo or a speaker wants and what
 * every other modern core does. The timer comes from the core's registry, so
 * tone() cannot silently steal the timer a Servo is using -- it would drag the
 * servo's 20 ms frame to an audio rate, and the servo would jitter with
 * nothing to explain it.
 *
 * A tone with a duration is ended by the timer's own update interrupt rather
 * than by a delay(), so tone(pin, freq, 100) returns immediately and the
 * sketch keeps running -- which is what Arduino promises and what a blocking
 * implementation quietly breaks.
 */
#include "Arduino.h"
#include "ch32h4_irq.h"
#include "ch32h4_pinmap.h"
#include "ch32h4_timer.h"

/* One tone at a time, as on every AVR-derived core. A second tone() on a
 * different pin stops the first. */
static struct {
    pin_size_t pin;
    uint8_t timer;
    uint8_t channel;
    volatile uint32_t remaining_ms;   /* 0 means "until noTone()" */
    bool active;
} s_tone = { (pin_size_t)-1, 0, 0, 0, false };

static void tone_stop(void) {
    if (!s_tone.active) {
        return;
    }
    TIM_TypeDef *dev = ch32h4_timer_dev(s_tone.timer);
    TIM_ITConfig(dev, TIM_IT_Update, DISABLE);
    TIM_Cmd(dev, DISABLE);
    ch32h4_timer_release(s_tone.timer, CH32H4_TIMER_TONE);

    /* Leave the pin low, not floating and not parked high: a speaker held at a
     * DC level draws current and can buzz. */
    pinMode(s_tone.pin, OUTPUT);
    digitalWrite(s_tone.pin, LOW);

    s_tone.active = false;
    s_tone.timer = 0;
}

/* C++ linkage, matching ArduinoCore-API's Common.h. That header declares these
 * with a default argument (`duration = 0`), which puts them OUTSIDE its
 * extern "C" block -- so an extern "C" definition here is a linkage conflict,
 * not a match. */
void noTone(uint8_t pin) {
    if (s_tone.active && s_tone.pin == pin) {
        tone_stop();
    }
}

void tone(uint8_t pin, unsigned int frequency, unsigned long duration) {
    if (pin >= PINS_COUNT || frequency == 0) {
        noTone(pin);
        return;
    }

    /* Starting a second tone stops the first, so the timer is released before
     * the search below rather than competing with it. */
    if (s_tone.active) {
        tone_stop();
    }

    /* Find a timer this pin can use that tone can have. Unlike analogWrite,
     * joining a timer someone else owns is never right here: the period is the
     * frequency. */
    uint8_t timer = 0, channel = 0, af = 0;
    for (size_t i = 0; i < g_pwm_af_map_len; i++) {
        const ch32h4_pwm_af_t *e = &g_pwm_af_map[i];
        if (e->pin != (uint8_t)pin || e->negated) {
            continue;
        }
        if (ch32h4_timer_owner(e->timer) == CH32H4_TIMER_FREE) {
            timer = e->timer;
            channel = e->channel;
            af = e->af;
            break;
        }
    }
    if (timer == 0 || !ch32h4_timer_claim(timer, CH32H4_TIMER_TONE)) {
        /* Either the pin has no timer channel, or every one it could use is
         * held by PWM, Servo or I2S. Doing nothing is the Arduino convention;
         * ch32h4_timer_owner() is how a caller finds out which. */
        return;
    }

    s_tone.pin = pin;
    s_tone.timer = timer;
    s_tone.channel = channel;
    s_tone.remaining_ms = duration;
    s_tone.active = true;

    ch32h4_timer_clock_enable(timer);
    ch32h4_timer_reset(timer);

    TIM_TypeDef *dev = ch32h4_timer_dev(timer);

    /* Aim for a period of 1000 counts so the 50% duty is exact, then pick the
     * prescaler that puts the update rate at the requested frequency. Timers
     * divide HCLK, never SystemCoreClock. */
    uint32_t top = 1000;
    uint32_t prescaler = ch32h4_timer_input_clock(timer) / (frequency * top);
    if (prescaler == 0) {
        /* Too fast for a 1000-count period: shorten the period instead of
         * giving the caller the wrong note. */
        prescaler = 1;
        top = ch32h4_timer_input_clock(timer) / frequency;
        if (top > 0xFFFF) {
            top = 0xFFFF;
        }
    } else if (prescaler > 0x10000u) {
        prescaler = 0x10000u;
    }

    TIM_TimeBaseInitTypeDef t = {0};
    t.TIM_Prescaler = (uint16_t)(prescaler - 1u);
    t.TIM_CounterMode = TIM_CounterMode_Up;
    t.TIM_Period = (uint16_t)(top - 1u);
    t.TIM_ClockDivision = TIM_CKD_DIV1;
    TIM_TimeBaseInit(dev, &t);

    TIM_OCInitTypeDef oc = {0};
    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OutputState = TIM_OutputState_Enable;
    oc.TIM_Pulse = (uint16_t)(top / 2u);      /* 50% */
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    switch (channel) {
        case 1: TIM_OC1Init(dev, &oc); break;
        case 2: TIM_OC2Init(dev, &oc); break;
        case 3: TIM_OC3Init(dev, &oc); break;
        default: TIM_OC4Init(dev, &oc); break;
    }

    ch32h4_pin_af(g_pins[pin].port, g_pins[pin].bit, af, CH32H4_CFG_AF_PP_50);

    /* TIM1 and TIM8 keep their outputs electrically disabled until MOE is set,
     * and nothing says so. */
    if (timer == 1 || timer == 8) {
        TIM_CtrlPWMOutputs(dev, ENABLE);
    }

    if (duration > 0) {
        /* Count the tone out on the timer's own update event, so tone() with a
         * duration returns immediately. The update rate IS the tone frequency,
         * so the ISR counts periods rather than milliseconds. */
        TIM_ITConfig(dev, TIM_IT_Update, ENABLE);
        NVIC_EnableIRQ(timer == 2 ? TIM2_IRQn
                     : timer == 3 ? TIM3_IRQn
                     : timer == 4 ? TIM4_IRQn
                     : timer == 5 ? TIM5_IRQn
                                  : TIM2_IRQn);
        /* Periods, not milliseconds. */
        s_tone.remaining_ms = (duration * frequency) / 1000u;
        if (s_tone.remaining_ms == 0) {
            s_tone.remaining_ms = 1;
        }
    }

    TIM_Cmd(dev, ENABLE);
}

/* The update handlers for the timers tone() can end up on. Each is weak in the
 * vector table, so defining them here overrides the spin-loop defaults. Only
 * the timer tone actually holds does anything. */
static void tone_tick(uint8_t timer) {
    TIM_TypeDef *dev = ch32h4_timer_dev(timer);
    if (TIM_GetITStatus(dev, TIM_IT_Update) == RESET) {
        return;
    }
    TIM_ClearITPendingBit(dev, TIM_IT_Update);

    if (!s_tone.active || s_tone.timer != timer || s_tone.remaining_ms == 0) {
        return;
    }
    if (--s_tone.remaining_ms == 0) {
        tone_stop();
    }
}

void CH32H4_IRQ_HANDLER(TIM2_IRQHandler);
void TIM2_IRQHandler(void) { tone_tick(2); }

void CH32H4_IRQ_HANDLER(TIM3_IRQHandler);
void TIM3_IRQHandler(void) { tone_tick(3); }

void CH32H4_IRQ_HANDLER(TIM4_IRQHandler);
void TIM4_IRQHandler(void) { tone_tick(4); }

void CH32H4_IRQ_HANDLER(TIM5_IRQHandler);
void TIM5_IRQHandler(void) { tone_tick(5); }
