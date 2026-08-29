/* Clock-enable, block-reset and pin/AF helpers.
 *
 * These exist because -Wall catches none of this silicon's real failures. Each
 * one makes a specific silent failure unrepresentable rather than merely
 * documented:
 *
 *   - the bus and the peripheral are passed together, so they cannot be
 *     mismatched at a call site;
 *   - the clock enable always reads back, so the first-access-dropped bug
 *     cannot recur;
 *   - the reset helper refuses the blocks that must never be reset;
 *   - the pin helper sets the mode register and the AF mux together, and
 *     enables AFIO first.
 */
#pragma once

#include <stdint.h>
#include "ch32h417.h"

#ifdef __cplusplus
extern "C" {
#endif

/* RCC has three peripheral buses on this family and there is no APB at all.
 * The assignment is not what habit predicts:
 *
 *   HB2   USART1, SPI1, TIM1, TIM8-TIM12, ADC1, ADC2, GPIOA-GPIOF, AFIO
 *   HB1   SPI2, SPI3, I2C1, I2C2, TIM2-TIM7, DAC, PWR, BKP, SWPMI
 *   HB    DMA1, DMA2, SDMMC, RNG, ETH, OTG_FS, FMC
 *
 * Enabling a bit on the wrong bus produces no error whatsoever: the
 * peripheral's registers read back as zeroes and writes to them are silently
 * discarded. Half a day of "the peripheral is broken" in the libhal port was
 * one wrong bus.
 *
 * For all three buses the RCC_HBxPeriph_* constant occupies the same bit
 * position in the matching reset register, so one mask serves both. */
typedef enum {
    CH32_BUS_HB = 0,
    CH32_BUS_HB1,
    CH32_BUS_HB2,
} ch32_bus_t;

/* Enable a peripheral clock and read the register back.
 *
 * The read-back is not decoration. RCC_*PeriphClockCmd is a read-modify-write
 * with no read-back of its own, and an access to the peripheral immediately
 * after enabling its clock can be dropped. That bit four separate drivers in
 * the libhal port before it was factored into one place. */
void ch32h4_clock_enable(ch32_bus_t bus, uint32_t mask);

/* Pulse a peripheral's reset.
 *
 * Configuration on this part survives a warm reset, the debugger's reset and a
 * re-flash -- only a power cycle clears it -- so a driver that does not reset
 * its block inherits the previous run's configuration.
 *
 * Returns 0 if the reset was performed, -1 if it was refused. These are
 * refused, and each for a concrete reason:
 *
 *   GPIOx, AFIO   shared by every driver: resetting GPIOB for I2C1 wipes the
 *                 pins SPI or I2S already configured
 *   PWR, BKP      drops the VIO18 rail and discards the RTC's backup domain
 *   DMA1          channels are handed out to several drivers at once
 *   ETH           hangs the boot in the MACPHYCR write two lines later, with
 *                 no fault and no timeout, and no delay fixes it.
 *                 ETH_SoftwareReset() is the reset that block documents.
 */
int ch32h4_block_reset(ch32_bus_t bus, uint32_t mask);

/* How many resets have been refused. Exposed so a test can prove the refusal
 * happens rather than trusting that it does. */
extern uint32_t ch32h4_reset_refused_count;

/* No alternate function: leave the AF mux alone and set only the mode. */
#define CH32H4_AF_NONE  0xFFu

/* F1-style four-bit mode-register encodings, which is what CFGLR/CFGHR take. */
#define CH32H4_CFG_IN_ANALOG     0x0u
#define CH32H4_CFG_IN_FLOATING   0x4u
#define CH32H4_CFG_IN_PUPD       0x8u
#define CH32H4_CFG_OUT_PP_50     0x3u
#define CH32H4_CFG_OUT_OD_50     0x7u
#define CH32H4_CFG_AF_PP_50      0xBu
#define CH32H4_CFG_AF_OD_50      0xFu

/* Configure a pin's mode register and its alternate-function mux together.
 *
 * GPIO here is two mechanisms and both are required. The mode register is
 * STM32F1-style -- CFGLR/CFGHR, four bits per pin, direction and configuration
 * in one field. The AF mux is STM32F4-style, a separate write.
 *
 * Setting AF_PP without the mux gives a peripheral that runs, whose every
 * status flag is correct, and nothing on the wire. This happened on I2C1 and
 * again on USART1 in the libhal port. The mux write is itself discarded if
 * AFIO's clock is off, so this enables AFIO first.
 *
 * Pass CH32H4_AF_NONE for a plain GPIO or an analog pin.
 *
 * Note that an input a peripheral reads must also be an AF mode, not a
 * floating input: the mux owns the pad's output enable. And open-drain here
 * has NO internal pull-up -- the F1-style encoding does not offer one -- so a
 * 1-Wire or I2C line needs a real resistor. */
void ch32h4_pin_af(GPIO_TypeDef *port, uint8_t pin, uint8_t af, uint8_t mode);

/* The RCC clock-enable bit for a GPIO port. */
uint32_t ch32h4_gpio_clock_bit(GPIO_TypeDef *port);

#ifdef __cplusplus
}
#endif
