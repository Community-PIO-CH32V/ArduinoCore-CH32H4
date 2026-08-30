/* What the V3F does.
 *
 * It boots first, brings the part up, hands control to the V5F and sleeps.
 * In M4 the sleep is replaced by setup1()/loop1().
 */
#include "ch32h4_clock.h"
#include "ch32h4_console.h"
#include "ch32h4_xcore.h"
#include "ch32h417.h"

/* NVIC_WakeUp_V5F() masks the address with ~0x3FF and does not complain, so an
 * unaligned entry starts the core in the middle of whatever precedes it. */
_Static_assert(CH32_V5F_START_ADDR % 1024 == 0,
               "CH32_V5F_START_ADDR must be 1 KB aligned");

/* Defined by the linker script. Checked against the build constant at run time
 * because the two are separate statements of one address and nothing else
 * compares them. */
extern char _v5f_entry[];

/* Weak: a single-core sketch defines neither, and this core sleeps. */
extern void setup1(void) __attribute__((weak));
extern void loop1(void) __attribute__((weak));

#define CH32H4_V3F_TEXT  __attribute__((section(".text.v3f")))

/* The VIO18 rail must be set before ANY pin is configured, including the
 * console's. PWR_CTLR bits [12:10] select 1.2 / 1.8 / 2.5 / 3.3 V and bit [9]
 * chooses that field over the power-up default. The rail rises fast and falls
 * only by leakage -- nothing discharges it -- so raising it is a one-way step
 * in practice, and 3.3 V is the setting that measures exact. */
CH32H4_V3F_TEXT
static void vio18_init(void) {
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR, ENABLE);
    (void)RCC->HB1PCENR;   /* read back, or the first PWR access is dropped */

    uint32_t ctlr = PWR->CTLR;
    ctlr &= ~(7u << 10);
    ctlr |= (3u << 10) | (1u << 9);
    PWR->CTLR = ctlr;
}

CH32H4_V3F_TEXT
void ch32h4_v3f_main(void) {
    vio18_init();
    ch32h4_clock_init();
    ch32h4_console_init(115200);

    ch32h4_console_puts("\nCH32H4 Arduino core\n");

    /* Say which reference we got, loudly. On the internal RC the board runs
     * and looks healthy, but the Ethernet PLL never locks and USB is out of
     * spec, and every failure downstream then presents as a different bug. */
    ch32h4_console_puts(
        ch32h4_clock_source() == CH32H4_CLOCK_SRC_HSE ? "sysclk_src=hse\n"
                                                      : "sysclk_src=hsi\n");
    ch32h4_console_puts("hclk=");
    ch32h4_console_putu(ch32h4_hclk());
    ch32h4_console_puts("\nsysclk=");
    ch32h4_console_putu(ch32h4_sysclk());
    ch32h4_console_puts("\n");

    if (!ch32h4_clock_is_nominal()) {
        ch32h4_console_puts(
            "WARNING: degraded clock -- Ethernet and USB will not work\n");
    }

    if ((uint32_t)_v5f_entry != (uint32_t)CH32_V5F_START_ADDR) {
        /* The linker and the build constant disagree, which means the wake
         * would start the V5F at the wrong address. Stop here: it is the one
         * failure this core can still report. */
        ch32h4_console_puts("FATAL: _v5f_entry != CH32_V5F_START_ADDR (");
        ch32h4_console_puthex((uint32_t)_v5f_entry);
        ch32h4_console_puts(" vs ");
        ch32h4_console_puthex((uint32_t)CH32_V5F_START_ADDR);
        ch32h4_console_puts(")\n");
        for (;;) {
        }
    }

    ch32h4_console_puts("boot ok\n");

    /* XCORE_RAM is NOLOAD, so nothing has initialised it. Clear the ready
     * flag and both FIFO rings before the wake -- this is the only moment at
     * which one core can safely touch state the other will use. */
    ch32h4_runtime_ready = 0;
    ch32h4_xcore_init();

    ch32h4_console_puts("V3F: waking V5F\n");
    NVIC_WakeUp_V5F((uint32_t)CH32_V5F_START_ADDR);

    /* If the sketch defines setup1() or loop1(), this core runs them.
     *
     * Wait for the V5F first. A sketch's globals are constructed over there,
     * in .init_array, and setup1() touching one before that finishes would
     * read an unconstructed object -- a bug that would appear and disappear
     * with the relative speed of the two cores, which is the worst kind. */
    if (setup1 || loop1) {
        ch32h4_console_puts("V3F: waiting for the V5F runtime\n");
        ch32h4_console_flush();

        uint32_t guard = 0;
        while (ch32h4_runtime_ready != CH32H4_RUNTIME_READY_MAGIC) {
            if (++guard > 200000000u) {
                /* The V5F never got there. Say so rather than sitting in a
                 * loop that looks identical to a working idle core. */
                ch32h4_console_puts("V3F: FATAL, V5F never signalled ready\n");
                ch32h4_console_flush();
                break;
            }
        }

        ch32h4_console_puts("V3F: running setup1/loop1\n");
        ch32h4_console_flush();

        if (setup1) {
            setup1();
        }
        for (;;) {
            if (loop1) {
                loop1();
            }
        }
    }

    /* Otherwise this core has nothing to do, so it sleeps.
     *
     * PWR_EnterSTOPMode rather than a bare WFI, and in a loop. Stop mode takes
     * effect only when BOTH cores request it, and this helper clears its own
     * SLEEPDEEP on the way out -- a plain WFI would leave this core in shallow
     * sleep after any wake, so a later deepsleep would silently degrade to
     * Sleep mode and stop saving the power that is its whole point. The helper
     * returns whenever a wake source fires, hence the loop; a `nop` loop here
     * would spin the core at full clock forever. */
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR, ENABLE);
    (void)RCC->HB1PCENR;
    for (;;) {
        PWR_EnterSTOPMode(PWR_Regulator_LowPower, PWR_STOPEntry_WFE);
    }
}
