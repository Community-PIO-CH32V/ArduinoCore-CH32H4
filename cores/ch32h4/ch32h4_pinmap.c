#include "Arduino.h"
#include "ch32h4_pinmap.h"
#include "ch32h4_timer.h"

bool ch32h4_pin_has_pwm(pin_size_t pin) {
    for (size_t i = 0; i < g_pwm_af_map_len; i++) {
        if (g_pwm_af_map[i].pin == pin) {
            return true;
        }
    }
    return false;
}

bool ch32h4_pwm_find_on_timer(pin_size_t pin, uint8_t timer, uint8_t channel,
                              ch32h4_pwm_af_t *out) {
    for (size_t i = 0; i < g_pwm_af_map_len; i++) {
        const ch32h4_pwm_af_t *e = &g_pwm_af_map[i];
        if (e->pin == pin && e->timer == timer && e->channel == channel) {
            if (out) {
                *out = *e;
            }
            return true;
        }
    }
    return false;
}

bool ch32h4_pwm_find(pin_size_t pin, ch32h4_pwm_af_t *out) {
    const ch32h4_pwm_af_t *first_free = NULL;

    for (size_t i = 0; i < g_pwm_af_map_len; i++) {
        const ch32h4_pwm_af_t *e = &g_pwm_af_map[i];
        if (e->pin != pin) {
            continue;
        }
        /* Never chosen automatically: a complementary output shares its
         * compare register with its twin and idles inverted, so handing one
         * back silently would give the caller an inverted signal with nothing
         * to explain it. ch32h4_pwm_find_on_timer() will still return one. */
        if (e->negated) {
            continue;
        }

        const uint8_t owner = ch32h4_timer_owner(e->timer);

        /* Prefer a timer PWM already holds, so a second analogWrite() pin
         * joins the running timer instead of consuming another one. Twelve
         * timers go quickly when every call takes a fresh one. */
        if (owner == CH32H4_TIMER_PWM) {
            if (out) {
                *out = *e;
            }
            return true;
        }
        if (owner == CH32H4_TIMER_FREE && first_free == NULL) {
            first_free = e;
        }
    }

    if (first_free) {
        if (out) {
            *out = *first_free;
        }
        return true;
    }
    return false;
}

bool ch32h4_pwm_find_active(pin_size_t pin, ch32h4_pwm_af_t *out) {
    for (size_t i = 0; i < g_pwm_af_map_len; i++) {
        const ch32h4_pwm_af_t *e = &g_pwm_af_map[i];
        if (e->pin == pin && ch32h4_timer_owner(e->timer) == CH32H4_TIMER_PWM) {
            if (out) {
                *out = *e;
            }
            return true;
        }
    }
    return false;
}
