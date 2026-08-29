/* Clock tree.
 *
 *   SYSCLK / SystemClock  400 MHz   the V5F core clock
 *   HCLK                  100 MHz   SYSCLK >> 2 -- the bus clock
 *   ADCCLK                 12.5 MHz HCLK / 8
 *
 * There is no APB prescaler in the STM32 sense; RCC_ClocksTypeDef has no PCLK
 * field at all. SysTick counts at HCLK, and SPI, the timers and I2C all divide
 * HCLK. SystemCoreClock is NEVER the right number for a peripheral divider --
 * on the V5F it is four times HCLK, and getting it wrong is an error that no
 * self-consistent test on the board can detect, because every part of the
 * board agrees with every other part.
 *
 * Use ch32h4_hclk(). It is right on both cores.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CH32H4_CLOCK_SRC_HSI = 0,   /* internal RC: degraded, see below */
    CH32H4_CLOCK_SRC_HSE = 1,   /* the crystal: full speed */
} ch32h4_clock_src_t;

/* Programs the whole tree. The V3F calls this once, before the V5F is awake.
 * The V5F must NOT call it: reconfiguring PLLs underneath a running core is
 * exactly as bad as it sounds. */
void ch32h4_clock_init(void);

/* Which reference the part actually came up on.
 *
 * This matters far more than it looks. On the internal RC the board runs and
 * looks healthy, but the Ethernet PLL never locks and USB is out of spec, so
 * every failure downstream presents as a different bug. The vendor's
 * SetSysClock() leaves an empty `else` on this path -- literally commented
 * "User can add here some code to deal with this error" -- so nothing reports
 * it unless we do.
 *
 * Peripheral setup stays correct either way, because everything divides the
 * measured HCLK rather than an assumed constant. Only Ethernet and USB are
 * actually lost, and both check this before bringing themselves up. */
ch32h4_clock_src_t ch32h4_clock_source(void);

/* The bus clock. Divide THIS for any peripheral. 100 MHz on a good boot. */
uint32_t ch32h4_hclk(void);

/* The system clock: 400 MHz on a good boot. This is the V5F's core clock and
 * four times the V3F's. */
uint32_t ch32h4_sysclk(void);

/* True when the part came up on the crystal at the frequency the build asked
 * for. False means degraded: running, but not as configured. */
int ch32h4_clock_is_nominal(void);

#ifdef __cplusplus
}
#endif
