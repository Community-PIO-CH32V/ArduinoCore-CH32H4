/* The I2C peripherals, below any library.
 *
 * Same shape and same reason as ch32h4_spi.h: which registers an id names,
 * which bus gates its clock, how to reset it. The bus split is the trap here
 * -- I2C1, I2C2 and I2C3 are on HB1 and only I2C4 is on HB2, which is the
 * OPPOSITE way round from SPI -- and a peripheral clocked from the wrong bus
 * reads back as all zeroes and swallows every write without complaint.
 *
 * The interrupt half is in ch32h4_i2c_irq.c so that a sketch using only the
 * polled master does not link eight interrupt handlers it never reaches.
 * Nothing but ch32h4_i2c_attach_irq() pulls that file in.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ch32h417.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CH32H4_I2C_COUNT 4

/* Registers for peripheral `id` (1..4), or NULL. */
I2C_TypeDef *ch32h4_i2c_regs(uint8_t id);

/* Ungate the peripheral's clock on whichever bus carries it. */
void ch32h4_i2c_clock_enable(uint8_t id);

/* Pulse the peripheral's reset. Worth doing before every configuration on
 * this part: the BUSY latch survives a warm reset, the debugger's reset and a
 * re-flash, so a board reset mid-transfer otherwise comes back with a bus
 * that is permanently busy and no way to tell that from a missing device. */
void ch32h4_i2c_reset(uint8_t id);

/* The two interrupts each peripheral raises. They are separate vectors and
 * mean different things: EV is the protocol moving forward, ER is it going
 * wrong. */
IRQn_Type ch32h4_i2c_ev_irqn(uint8_t id);
IRQn_Type ch32h4_i2c_er_irqn(uint8_t id);

/* `error` distinguishes the two vectors, so one callback can serve both. */
typedef void (*ch32h4_i2c_irq_t)(uint8_t id, bool error, void *ctx);

/* Route both of peripheral `id`'s vectors to `fn`. One owner per peripheral:
 * a second caller replaces the first rather than chaining.
 *
 * Enables neither the NVIC entries nor any interrupt source; the caller
 * decides which of EVT, BUF and ERR it wants. */
void ch32h4_i2c_attach_irq(uint8_t id, ch32h4_i2c_irq_t fn, void *ctx);

/* Stop dispatching, and mask the peripheral's sources so a flag still set
 * cannot spin the NVIC against a handler that is now nobody's. */
void ch32h4_i2c_detach_irq(uint8_t id);

#ifdef __cplusplus
}
#endif
