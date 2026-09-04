/* See ch32h4_i2c.h. Helpers only; the interrupt dispatch is in
 * ch32h4_i2c_irq.c so that it links only when something attaches to it. */
#include "ch32h4_i2c.h"

#include "ch32h4_rcc.h"

I2C_TypeDef *ch32h4_i2c_regs(uint8_t id) {
    switch (id) {
        case 1: return I2C1;
        case 2: return I2C2;
        case 3: return I2C3;
        case 4: return I2C4;
        default: return (I2C_TypeDef *)0;
    }
}

/* I2C1, I2C2 and I2C3 are on HB1; only I2C4 is on HB2. The opposite split
 * from SPI, which is exactly why no call site is allowed to name it. */
static bool i2c_bus(uint8_t id, ch32_bus_t *bus, uint32_t *mask) {
    static const uint32_t hb1[3] = {
        RCC_HB1Periph_I2C1, RCC_HB1Periph_I2C2, RCC_HB1Periph_I2C3,
    };
    if (id == 4) {
        *bus = CH32_BUS_HB2;
        *mask = RCC_HB2Periph_I2C4;
        return true;
    }
    if (id >= 1 && id <= 3) {
        *bus = CH32_BUS_HB1;
        *mask = hb1[id - 1];
        return true;
    }
    return false;
}

void ch32h4_i2c_clock_enable(uint8_t id) {
    ch32_bus_t bus;
    uint32_t mask;
    if (i2c_bus(id, &bus, &mask)) {
        ch32h4_clock_enable(bus, mask);
    }
}

void ch32h4_i2c_reset(uint8_t id) {
    ch32_bus_t bus;
    uint32_t mask;
    if (i2c_bus(id, &bus, &mask)) {
        ch32h4_block_reset(bus, mask);
    }
}

IRQn_Type ch32h4_i2c_ev_irqn(uint8_t id) {
    switch (id) {
        case 1: return I2C1_EV_IRQn;
        case 2: return I2C2_EV_IRQn;
        case 3: return I2C3_EV_IRQn;
        case 4: return I2C4_EV_IRQn;
        default: return (IRQn_Type)0;
    }
}

IRQn_Type ch32h4_i2c_er_irqn(uint8_t id) {
    switch (id) {
        case 1: return I2C1_ER_IRQn;
        case 2: return I2C2_ER_IRQn;
        case 3: return I2C3_ER_IRQn;
        case 4: return I2C4_ER_IRQn;
        default: return (IRQn_Type)0;
    }
}
