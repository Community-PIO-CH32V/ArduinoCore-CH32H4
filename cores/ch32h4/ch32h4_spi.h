/* The SPI peripherals, below any library.
 *
 * Two libraries drive these -- SPI as a master, SPISlave as a slave -- and
 * both need the same three things: which registers a peripheral id names,
 * which bus gates its clock, and how to reset it. Duplicating that in each
 * library is how the two end up disagreeing, and the way they disagree is
 * silent: SPI1 is on HB2 while SPI2, SPI3 and SPI4 are on HB1, and a
 * peripheral clocked from the wrong bus reads back as all zeroes and discards
 * every write without setting an error bit anywhere.
 *
 * The interrupt half lives in ch32h4_spi_irq.c rather than here so that a
 * sketch using only the polled/DMA master does not link four interrupt
 * handlers it never reaches. Nothing but ch32h4_spi_attach_irq() pulls that
 * file in.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "ch32h417.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CH32H4_SPI_COUNT 4

/* Registers for peripheral `id` (1..4), or NULL if the id is not one. */
SPI_TypeDef *ch32h4_spi_regs(uint8_t id);

/* Ungate the peripheral's clock on whichever bus carries it. */
void ch32h4_spi_clock_enable(uint8_t id);

/* Pulse the peripheral's reset.
 *
 * Configuration survives a warm reset, the debugger's reset and a re-flash, so
 * a peripheral that is not reset here inherits the previous run's mode and
 * baud divider -- which is how a sketch that was a master last run comes up
 * clocking a bus it now expects to be clocked by someone else. */
void ch32h4_spi_reset(uint8_t id);

/* The global interrupt for peripheral `id`, or 0. */
IRQn_Type ch32h4_spi_irqn(uint8_t id);

/* Called from SPIx_IRQHandler with the peripheral id and the caller's context.
 * Runs in interrupt context: nothing here may block, and anything it touches
 * that the sketch also touches needs the usual care. */
typedef void (*ch32h4_spi_irq_t)(uint8_t id, void *ctx);

/* Route SPIx_IRQHandler to `fn`. One owner per peripheral -- the second caller
 * replaces the first rather than chaining, because two drivers on one SPI is
 * not a working configuration and pretending otherwise hides it.
 *
 * This does NOT enable any interrupt source or the NVIC entry; the caller
 * decides which of TXE, RXNE and ERR it wants. */
void ch32h4_spi_attach_irq(uint8_t id, ch32h4_spi_irq_t fn, void *ctx);

/* Stop dispatching, and mask the peripheral's interrupt sources so a flag
 * still set cannot spin the NVIC against a handler that is now nobody's. */
void ch32h4_spi_detach_irq(uint8_t id);

#ifdef __cplusplus
}
#endif
