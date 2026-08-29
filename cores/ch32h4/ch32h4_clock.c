#include "ch32h4_clock.h"
#include "ch32h417.h"
#include "system_ch32h417.h"

/* The frequency the build asked for. The platform's common_clk_config.py
 * derives SYSCLK_400M_CoreCLK_V5F_400M_V3F_100M_HSE from the board's f_cpu and
 * clock_source, and system_ch32h417_v3f.c programs that tree. */
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
