/* Pin -> peripheral alternate-function maps.
 *
 * The tables themselves live in the variant (variants/<board>/pin_map.c),
 * because which pads exist is a property of the package and a board may
 * withhold one the silicon would allow. The core only knows how to search
 * them.
 *
 * Everything here is a lookup, not a claim. Claiming the timer is
 * ch32h4_timer_claim()'s job, and the two are separate so a caller can ask
 * "could this pin do PWM?" without taking anything.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "api/Common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t pin;      /* Arduino pin number */
    uint8_t timer;    /* 1-12 */
    uint8_t channel;  /* 1-4 */
    bool    negated;  /* the pad is TIMx_CHyN, the complementary output */
    uint8_t af;       /* alternate-function number for GPIO_PinAFConfig */
} ch32h4_pwm_af_t;

/* Provided by the variant. */
extern const ch32h4_pwm_af_t g_pwm_af_map[];
extern const size_t g_pwm_af_map_len;

/* The best PWM option for a pin.
 *
 * Prefers, in order: a timer already owned by PWM (so a second analogWrite()
 * pin joins a timer that is already running rather than taking a new one), then
 * any free timer. Negated pads are never chosen automatically -- they share a
 * compare register with their non-negated twin and idle inverted, so picking
 * one silently would give a caller an inverted output for no visible reason.
 *
 * Returns false if the pin has no timer channel, or if every timer it could use
 * is owned by something else.
 */
bool ch32h4_pwm_find(pin_size_t pin, ch32h4_pwm_af_t *out);

/* The entry for one specific timer and channel, negated pads included.
 * For callers that know exactly what they want -- Servo pinning itself to a
 * timer, or a driver that deliberately wants the complementary output. */
bool ch32h4_pwm_find_on_timer(pin_size_t pin, uint8_t timer, uint8_t channel,
                              ch32h4_pwm_af_t *out);

/* The entry for a pin whose timer is currently owned by PWM -- i.e. the one
 * analogWrite() actually configured. Used to release the right channel again.
 * Returns false if nothing is driving this pin. */
bool ch32h4_pwm_find_active(pin_size_t pin, ch32h4_pwm_af_t *out);

/* Whether a pin has any timer channel at all. Cheap; for a library that wants
 * to validate its configuration before touching hardware. */
bool ch32h4_pin_has_pwm(pin_size_t pin);

#ifdef __cplusplus
}
#endif
