/* The Arduino pin number -> hardware mapping.
 *
 * Pin numbers are dense and port-ordered: PA0..PA15 are 0..15, PB0..PB15 are
 * 16..31, PC0..PC15 are 32..47. The variant defines the PAn/PBn/PCn macros and
 * the g_pins table.
 */
#pragma once

#include <stdint.h>
#include "ch32h417.h"
#include "api/Common.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    GPIO_TypeDef *port;
    uint8_t       bit;
    /* ADC1 channel, or 0xFF where the pin has no analog input. */
    uint8_t       adc_channel;
} ch32h4_pin_t;

/* Defined by the variant. PINS_COUNT comes from pins_arduino.h. */
extern const ch32h4_pin_t g_pins[];

/* Which Arduino pin currently owns EXTI line `line`, or -1.
 *
 * EXTI lines are shared by pin NUMBER across every port -- PA0, PB0 and PC0
 * all map to line 0 -- so only one of them may have an interrupt at a time.
 * attachInterrupt refuses the second rather than silently stealing the line
 * from a driver that is working. */
int ch32h4_exti_owner(uint8_t line);

#ifdef __cplusplus
}
#endif
