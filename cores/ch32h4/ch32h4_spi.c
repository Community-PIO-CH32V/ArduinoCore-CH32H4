/* See ch32h4_spi.h. Helpers only; the interrupt dispatch is in
 * ch32h4_spi_irq.c so that it links only when something attaches to it. */
#include "ch32h4_spi.h"

#include "ch32h4_rcc.h"

SPI_TypeDef *ch32h4_spi_regs(uint8_t id) {
    switch (id) {
        case 1: return SPI1;
        case 2: return SPI2;
        case 3: return SPI3;
        case 4: return SPI4;
        default: return (SPI_TypeDef *)0;
    }
}

/* SPI1 is on HB2; SPI2, SPI3 and SPI4 are on HB1. The split runs the opposite
 * way from I2C, which is exactly why no call site is allowed to name it. */
static bool spi_bus(uint8_t id, ch32_bus_t *bus, uint32_t *mask) {
    static const uint32_t hb1[3] = {
        RCC_HB1Periph_SPI2, RCC_HB1Periph_SPI3, RCC_HB1Periph_SPI4,
    };
    if (id == 1) {
        *bus = CH32_BUS_HB2;
        *mask = RCC_HB2Periph_SPI1;
        return true;
    }
    if (id >= 2 && id <= CH32H4_SPI_COUNT) {
        *bus = CH32_BUS_HB1;
        *mask = hb1[id - 2];
        return true;
    }
    return false;
}

void ch32h4_spi_clock_enable(uint8_t id) {
    ch32_bus_t bus;
    uint32_t mask;
    if (spi_bus(id, &bus, &mask)) {
        ch32h4_clock_enable(bus, mask);
    }
}

void ch32h4_spi_reset(uint8_t id) {
    ch32_bus_t bus;
    uint32_t mask;
    if (spi_bus(id, &bus, &mask)) {
        ch32h4_block_reset(bus, mask);
    }
}

IRQn_Type ch32h4_spi_irqn(uint8_t id) {
    switch (id) {
        case 1: return SPI1_IRQn;
        case 2: return SPI2_IRQn;
        case 3: return SPI3_IRQn;
        case 4: return SPI4_IRQn;
        default: return (IRQn_Type)0;
    }
}
