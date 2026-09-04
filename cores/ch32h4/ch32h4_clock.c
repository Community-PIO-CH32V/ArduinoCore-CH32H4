#include "ch32h4_clock.h"
#include "ch32h417.h"
#include "system_ch32h417.h"

/* The clock tree comes from a macro the BUILD defines, and nothing in the
 * sources supplies a default. Under PlatformIO the platform's
 * common_clk_config.py derives SYSCLK_400M_CoreCLK_V5F_400M_V3F_100M_HSE from
 * the board's f_cpu and clock_source; under the Arduino IDE it is
 * build.flags.clock in boards.txt.
 *
 * If none is defined, system_ch32h417_v3f.c takes its #else branch, SetSysClock
 * programs nothing, and the part stays on the 25 MHz internal RC -- a sixteenth
 * of the intended speed, with no build error and nothing on the console except
 * a clock warning that reads like a dead crystal. That is exactly how the first
 * Arduino IDE build of this core came out, so the omission is a hard error
 * here rather than something to be noticed on the wire.
 */
#if !defined(SYSCLK_400M_CoreCLK_V5F_400M_V3F_100M_HSE) && \
    !defined(SYSCLK_400M_CoreCLK_V5F_400M_V3F_100M_HSI) && \
    !defined(SYSCLK_480M_CoreCLK_V5F_240M_V3F_120M_HSE) && \
    !defined(SYSCLK_480M_CoreCLK_V5F_240M_V3F_120M_HSI) && \
    !defined(SYSCLK_480M_CoreCLK_V5F_480M_V3F_120M_HSE) && \
    !defined(SYSCLK_480M_CoreCLK_V5F_480M_V3F_120M_HSI)
#error "No SYSCLK_* clock macro is defined. Without one the part runs from the \
25 MHz internal RC and no build system reports it. PlatformIO: set \
board_build.f_cpu and board_build.clock_source. Arduino IDE: the board is \
missing build.flags.clock -- see boards.txt."
#endif

/* The V3F's HCLK in the 400 MHz configuration. Only that configuration is
 * supported: the 480 MHz trees put the V3F at 120 MHz, which this constant
 * would have to follow. */
#define CH32H4_EXPECTED_HCLK   100000000u

static ch32h4_clock_src_t s_src = CH32H4_CLOCK_SRC_HSI;

void ch32h4_clock_init(void) {
    /* SystemInit() must be called explicitly. The .load stub only brings up a
     * 70 MHz bootstrap PLL, and without this the part stays there while every
     * peripheral divider is computed against the wrong number. 70 MHz is
     * plausible enough that a self-consistent test would not notice. */
    SystemInit();
    SystemAndCoreClockUpdate();

    /* SetSysClock()'s HSE path ends in an empty `else` -- the vendor's comment
     * there is "User can add here some code to deal with this error" -- so on
     * a dead crystal it simply leaves the part on the internal RC without a
     * PLL and returns normally. Read the hardware rather than trusting that it
     * did what was asked. */
    s_src = (RCC->CTLR & RCC_HSERDY) ? CH32H4_CLOCK_SRC_HSE
                                     : CH32H4_CLOCK_SRC_HSI;
}

ch32h4_clock_src_t ch32h4_clock_source(void) {
    return s_src;
}

uint32_t ch32h4_hclk(void) {
    return HCLKClock;
}

uint32_t ch32h4_sysclk(void) {
    return SystemClock;
}

int ch32h4_clock_is_nominal(void) {
    return (s_src == CH32H4_CLOCK_SRC_HSE) && (HCLKClock == CH32H4_EXPECTED_HCLK);
}
