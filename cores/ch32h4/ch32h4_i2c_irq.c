/* I2CxEV and I2CxER handlers for all four peripherals, dispatched to whoever
 * attached.
 *
 * Separate from ch32h4_i2c.c on purpose. The vector table's entries are weak
 * symbols already satisfied by the startup file's defaults, so the linker
 * pulls this object in only when something calls ch32h4_i2c_attach_irq(). A
 * sketch using the polled master pays nothing for it.
 */
#include <stddef.h>

#include "ch32h4_i2c.h"
#include "ch32h4_irq.h"

typedef struct {
    ch32h4_i2c_irq_t fn;
    void *ctx;
} i2c_irq_slot_t;

/* Indexed by peripheral id, so slot 0 is unused and ids read naturally. */
static i2c_irq_slot_t s_irq[CH32H4_I2C_COUNT + 1];

static bool valid(uint8_t id) {
    return id >= 1 && id <= CH32H4_I2C_COUNT;
}

void ch32h4_i2c_attach_irq(uint8_t id, ch32h4_i2c_irq_t fn, void *ctx) {
    if (!valid(id)) {
        return;
    }
    s_irq[id].ctx = ctx;
    /* The function pointer last: an interrupt arriving mid-update would
     * otherwise call the new handler with the old context. */
    __asm volatile ("" ::: "memory");
    s_irq[id].fn = fn;
}

void ch32h4_i2c_detach_irq(uint8_t id) {
    if (!valid(id)) {
        return;
    }
    s_irq[id].fn = NULL;

    /* Mask the sources as well as the dispatch. An I2C event flag is cleared
     * by the sequence that services it -- reading the data register, or
     * reading SR1 and then SR2 -- so a detached peripheral with ADDR or RXNE
     * still set re-enters the NVIC forever, and with no owner nobody performs
     * the sequence that would clear it. */
    I2C_TypeDef *dev = ch32h4_i2c_regs(id);
    if (dev) {
        I2C_ITConfig(dev, I2C_IT_EVT | I2C_IT_BUF | I2C_IT_ERR, DISABLE);
    }
}

static void i2c_dispatch(uint8_t id, bool error) {
    ch32h4_i2c_irq_t fn = s_irq[id].fn;
    if (fn) {
        fn(id, error, s_irq[id].ctx);
        return;
    }
    /* Nobody home, and the flag that raised this is still set. Mask the
     * sources rather than returning into an immediate re-entry. */
    ch32h4_i2c_detach_irq(id);
}

void CH32H4_IRQ_HANDLER(I2C1_EV_IRQHandler);
void I2C1_EV_IRQHandler(void) { i2c_dispatch(1, false); }
void CH32H4_IRQ_HANDLER(I2C1_ER_IRQHandler);
void I2C1_ER_IRQHandler(void) { i2c_dispatch(1, true); }

void CH32H4_IRQ_HANDLER(I2C2_EV_IRQHandler);
void I2C2_EV_IRQHandler(void) { i2c_dispatch(2, false); }
void CH32H4_IRQ_HANDLER(I2C2_ER_IRQHandler);
void I2C2_ER_IRQHandler(void) { i2c_dispatch(2, true); }

void CH32H4_IRQ_HANDLER(I2C3_EV_IRQHandler);
void I2C3_EV_IRQHandler(void) { i2c_dispatch(3, false); }
void CH32H4_IRQ_HANDLER(I2C3_ER_IRQHandler);
void I2C3_ER_IRQHandler(void) { i2c_dispatch(3, true); }

void CH32H4_IRQ_HANDLER(I2C4_EV_IRQHandler);
void I2C4_EV_IRQHandler(void) { i2c_dispatch(4, false); }
void CH32H4_IRQ_HANDLER(I2C4_ER_IRQHandler);
void I2C4_ER_IRQHandler(void) { i2c_dispatch(4, true); }
