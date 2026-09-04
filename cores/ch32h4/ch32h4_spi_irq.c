/* SPIx_IRQHandler for all four peripherals, dispatched to whoever attached.
 *
 * Separate from ch32h4_spi.c on purpose. The vector table's entries are weak
 * symbols already satisfied by the startup file's defaults, so the linker
 * pulls this object in only when something actually calls
 * ch32h4_spi_attach_irq(). A sketch using the polled or DMA master pays
 * nothing for it.
 */
#include "ch32h4_spi.h"

#include <stddef.h>

#include "ch32h4_irq.h"

typedef struct {
    ch32h4_spi_irq_t fn;
    void *ctx;
} spi_irq_slot_t;

/* Indexed by peripheral id, so slot 0 is unused and ids read naturally. */
static spi_irq_slot_t s_irq[CH32H4_SPI_COUNT + 1];

static bool valid(uint8_t id) {
    return id >= 1 && id <= CH32H4_SPI_COUNT;
}

void ch32h4_spi_attach_irq(uint8_t id, ch32h4_spi_irq_t fn, void *ctx) {
    if (!valid(id)) {
        return;
    }
    s_irq[id].ctx = ctx;
    /* The function pointer last: an interrupt arriving mid-update would
     * otherwise call the new handler with the old context. */
    __asm volatile ("" ::: "memory");
    s_irq[id].fn = fn;
}

void ch32h4_spi_detach_irq(uint8_t id) {
    if (!valid(id)) {
        return;
    }
    s_irq[id].fn = NULL;

    /* Mask the sources as well as the dispatch. A slave that is detached with
     * RXNE still set would otherwise re-enter the NVIC forever against a
     * handler that now does nothing -- the flag is cleared by reading the data
     * register, and with no owner nobody reads it. */
    SPI_TypeDef *dev = ch32h4_spi_regs(id);
    if (dev) {
        /* Through the SDK rather than as one masked write to CTLR2: the
         * SPI_I2S_IT_* values are encoded source indices, not bit positions
         * in that register, and using them as a mask disables the wrong
         * three bits. */
        SPI_I2S_ITConfig(dev, SPI_I2S_IT_TXE, DISABLE);
        SPI_I2S_ITConfig(dev, SPI_I2S_IT_RXNE, DISABLE);
        SPI_I2S_ITConfig(dev, SPI_I2S_IT_ERR, DISABLE);
    }
}

static void spi_dispatch(uint8_t id) {
    ch32h4_spi_irq_t fn = s_irq[id].fn;
    if (fn) {
        fn(id, s_irq[id].ctx);
        return;
    }
    /* Nobody home, but the flag that raised this is still set. Mask the
     * sources rather than returning into an immediate re-entry. */
    ch32h4_spi_detach_irq(id);
}

void CH32H4_IRQ_HANDLER(SPI1_IRQHandler);
void SPI1_IRQHandler(void) { spi_dispatch(1); }

void CH32H4_IRQ_HANDLER(SPI2_IRQHandler);
void SPI2_IRQHandler(void) { spi_dispatch(2); }

void CH32H4_IRQ_HANDLER(SPI3_IRQHandler);
void SPI3_IRQHandler(void) { spi_dispatch(3); }

void CH32H4_IRQ_HANDLER(SPI4_IRQHandler);
void SPI4_IRQHandler(void) { spi_dispatch(4); }
