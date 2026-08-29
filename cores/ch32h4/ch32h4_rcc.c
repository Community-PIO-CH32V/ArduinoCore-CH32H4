#include "ch32h4_rcc.h"

uint32_t ch32h4_reset_refused_count = 0;

static volatile uint32_t *clk_reg(ch32_bus_t bus) {
    switch (bus) {
        case CH32_BUS_HB:  return &RCC->HBPCENR;
        case CH32_BUS_HB1: return &RCC->HB1PCENR;
        default:           return &RCC->HB2PCENR;
    }
}

static volatile uint32_t *rst_reg(ch32_bus_t bus) {
    switch (bus) {
        case CH32_BUS_HB:  return &RCC->HBRSTR;
        case CH32_BUS_HB1: return &RCC->HB1PRSTR;
        default:           return &RCC->HB2PRSTR;
    }
}

void ch32h4_clock_enable(ch32_bus_t bus, uint32_t mask) {
    volatile uint32_t *reg = clk_reg(bus);
    *reg |= mask;
    (void)*reg;   /* or the first access to the peripheral is dropped */
}

int ch32h4_block_reset(ch32_bus_t bus, uint32_t mask) {
    static const uint32_t hb2_forbidden =
        RCC_HB2Periph_GPIOA | RCC_HB2Periph_GPIOB | RCC_HB2Periph_GPIOC
        | RCC_HB2Periph_GPIOD | RCC_HB2Periph_GPIOE | RCC_HB2Periph_GPIOF
        | RCC_HB2Periph_AFIO;

    if (bus == CH32_BUS_HB2 && (mask & hb2_forbidden)) {
        ch32h4_reset_refused_count++;
        return -1;
    }
    if (bus == CH32_BUS_HB1 && (mask & (RCC_HB1Periph_PWR | RCC_HB1Periph_BKP))) {
        ch32h4_reset_refused_count++;
        return -1;
    }
    if (bus == CH32_BUS_HB && (mask & (RCC_HBPeriph_DMA1 | RCC_HBPeriph_ETH))) {
        ch32h4_reset_refused_count++;
        return -1;
    }

    volatile uint32_t *reg = rst_reg(bus);
    *reg |= mask;
    (void)*reg;
    *reg &= ~mask;
    (void)*reg;
    return 0;
}

uint32_t ch32h4_gpio_clock_bit(GPIO_TypeDef *port) {
    if (port == GPIOA) return RCC_HB2Periph_GPIOA;
    if (port == GPIOB) return RCC_HB2Periph_GPIOB;
    if (port == GPIOC) return RCC_HB2Periph_GPIOC;
    if (port == GPIOD) return RCC_HB2Periph_GPIOD;
    if (port == GPIOE) return RCC_HB2Periph_GPIOE;
    return RCC_HB2Periph_GPIOF;
}

void ch32h4_pin_af(GPIO_TypeDef *port, uint8_t pin, uint8_t af, uint8_t mode) {
    /* The port's own clock, and AFIO's -- without the latter the AF mux write
     * below is silently discarded. */
    ch32h4_clock_enable(CH32_BUS_HB2,
                        ch32h4_gpio_clock_bit(port) | RCC_HB2Periph_AFIO);

    if (af != CH32H4_AF_NONE) {
        GPIO_PinAFConfig(port, pin, af);
    }

    volatile uint32_t *cfg = (pin < 8) ? &port->CFGLR : &port->CFGHR;
    uint32_t shift = (uint32_t)(pin % 8u) * 4u;
    *cfg = (*cfg & ~(0xFu << shift)) | ((uint32_t)mode << shift);
}
