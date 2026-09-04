/* Who owns each of the twelve timers.
 *
 * Several things want them and only one can have each, because they all set
 * the period: analogWrite() drives the compare channels, tone() drives the
 * update event, Servo needs a 20 ms frame of its own, I2S and a timer-paced
 * ADC each need their own rate. Two of those on one timer means one of them
 * silently runs at the other's frequency -- a servo twitching at 1 kHz because
 * analogWrite() got there first, with nothing to say why.
 *
 * So a timer is claimed before it is programmed, and a caller that cannot have
 * one is told rather than being given a broken one. Re-claiming by the same
 * owner succeeds, which is what lets a second analogWrite() pin join a timer
 * PWM already holds.
 *
 * This registry is the core's, not a library's, precisely so that libraries
 * added later -- Servo, I2S, Ticker, ADCInput -- contend through one place.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ch32h417.h"

#ifdef __cplusplus
extern "C" {
#endif

enum {
    CH32H4_TIMER_FREE = 0,
    CH32H4_TIMER_PWM,       /* analogWrite */
    CH32H4_TIMER_TONE,      /* tone() */
    CH32H4_TIMER_SERVO,
    CH32H4_TIMER_I2S,
    CH32H4_TIMER_ADC,       /* timer-paced sampling */
    CH32H4_TIMER_USER,      /* a sketch or library taking one directly */
    /* Appended, not inserted: these values are compared across separately
     * compiled libraries, so renumbering the existing ones would make an
     * old object file's "Servo" read as something else. */
    CH32H4_TIMER_TICKER,    /* Ticker's 1 kHz tick */
};

#define CH32H4_TIMER_COUNT  12

/* Take timer `id` (1..12) for `owner`. False if someone else already holds it.
 * Re-claiming by the current owner succeeds. */
bool ch32h4_timer_claim(uint8_t id, uint8_t owner);

/* Give it back. Does nothing if `owner` is not the current holder, so an
 * end()/detach() that runs twice is harmless. */
void ch32h4_timer_release(uint8_t id, uint8_t owner);

/* CH32H4_TIMER_FREE, or one of the owners above. */
uint8_t ch32h4_timer_owner(uint8_t id);

/* "free", "PWM", "tone", "Servo", "I2S", "ADC", "user" -- for a message that
 * says which subsystem is holding the timer a caller wanted. */
const char *ch32h4_timer_owner_name(uint8_t owner);

/* The peripheral for a timer id, or NULL. */
TIM_TypeDef *ch32h4_timer_dev(uint8_t id);

/* Enable a timer's clock on the correct bus.
 *
 * This is the whole reason the function exists. TIM1 and TIM8-TIM12 are on
 * HB2, but TIM2-TIM7 are on HB1 -- the split does not follow the STM32 habit
 * anyone would bring to this part, and enabling on the wrong bus produces a
 * timer whose registers read back as zeroes with no error whatsoever. */
void ch32h4_timer_clock_enable(uint8_t id);

/* Reset a timer's block, on the correct bus.
 *
 * Configuration survives a warm reset, the debugger's reset and a re-flash, so
 * a timer that is not reset inherits the previous run's period and mode. The
 * SDK's TIM_DeInit() is exactly this reset pulse and nothing else, whatever
 * its name suggests. */
void ch32h4_timer_reset(uint8_t id);

/* The update interrupt, dispatched to whoever owns the timer.
 *
 * Every TIMx_IRQHandler lives in ch32h4_timer.c and does one thing: look up
 * the handler registered for that timer and call it. It has to be that way --
 * a vector has exactly one definition, so the first subsystem to write
 * TIM4_IRQHandler makes the timer unusable to any other, and the second one
 * to try does not fail at run time, it fails to LINK. That is how tone() and
 * Ticker collided: both wanted the update event of a general-purpose timer.
 *
 * The handler runs in interrupt context with the pending bit already cleared.
 * Registering does not enable anything; the caller still sets TIM_IT_Update
 * and the NVIC, because only it knows when the timer is configured enough to
 * start taking interrupts.
 */
typedef void (*ch32h4_timer_irq_t)(uint8_t id, void *ctx);

/* Register for timer `id`. Replaces any previous registration, which is what
 * a re-claim of the same timer by the same owner should do. */
void ch32h4_timer_attach_irq(uint8_t id, ch32h4_timer_irq_t fn, void *ctx);

/* Unregister and mask the update interrupt. Safe to call for a timer that was
 * never attached. */
void ch32h4_timer_detach_irq(uint8_t id);

/* The NVIC line for a timer's update event, so a caller does not repeat the
 * mapping. Returns a negative value for a timer whose update interrupt this
 * core does not route. */
int ch32h4_timer_irqn(uint8_t id);

/* The clock feeding a timer's prescaler. Timers divide HCLK on this part;
 * SystemCoreClock is four times that on the V5F and is never the right
 * number. */
uint32_t ch32h4_timer_input_clock(uint8_t id);

#ifdef __cplusplus
}
#endif
