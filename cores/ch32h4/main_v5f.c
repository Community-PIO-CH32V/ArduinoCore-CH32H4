/* What the V5F does: everything the sketch sees.
 *
 * The V3F has already set the VIO18 rail, programmed the clock tree, brought
 * up the console and initialised .data and .bss. This core picks up from
 * there, runs the static constructors and calls setup() and loop().
 */
#include "ch32h4_clock.h"
#include "ch32h4_console.h"
#include "ch32h4_xcore.h"
#include "ch32h417.h"
#include "system_ch32h417.h"

/* wiring_time.c */
void ch32h4_systick_init(void);

/* Lives in the shared region, which is the only memory both cores reach at
 * speed. It cannot live in .bss: the V3F zeroes .bss before this core is even
 * awake, and the V5F's I-cache is not coherent with anything, so cross-core
 * state must be somewhere both cores agree about. Its section is NOLOAD, so
 * nothing initialises it -- the V3F clears it explicitly before the wake. */
volatile uint32_t ch32h4_runtime_ready __attribute__((section(".xcore")));

extern void (*__init_array_start[])(void);
extern void (*__init_array_end[])(void);

/* Weak, so a sketch that defines neither still links. */
__attribute__((weak)) void setup(void) { }
__attribute__((weak)) void loop(void) { }

static void run_static_constructors(void) {
    for (void (**p)(void) = __init_array_start; p < __init_array_end; p++) {
        (*p)();
    }
}

void ch32h4_v5f_main(void) {
    /* NOT SystemInit(). The V3F has already programmed every PLL, and this
     * core must not touch them. Only the cached frequency variables are
     * refreshed -- and this function is core-aware, so SystemCoreClock comes
     * out as this core's 400 MHz rather than the V3F's 100 MHz. */
    SystemAndCoreClockUpdate();

    /* Flash "enhance mode" is the code accelerator, and it is OFF at reset.
     *
     * Without it this core executes from flash at roughly a 145th of its ITCM
     * speed -- flash is clocked at HCLK/2 = 50 MHz against a 400 MHz core, and
     * every fetch pays it. With it, XIP becomes usable and the whole
     * "run from flash, keep the RAM for the heap" strategy holds up.
     *
     * The vendor SDK exposes it but nothing calls it, and nothing reports that
     * it is off: the board simply runs, slowly. */
    FLASH_Enhance_Mode(ENABLE);

    /* millis()/micros()/delay() from here on. SysTick counts at HCLK, so this
     * must come after the clock variables are refreshed. */
    ch32h4_systick_init();

    ch32h4_console_puts("V5F: alive core_id=");
    ch32h4_console_putu(NVIC_GetCurrentCoreID());
    ch32h4_console_puts("\nV5F: sysclk=");
    ch32h4_console_putu(ch32h4_sysclk());
    ch32h4_console_puts("\nV5F: coreclk=");
    ch32h4_console_putu(SystemCoreClock);
    ch32h4_console_puts("\nV5F: hclk=");
    ch32h4_console_putu(ch32h4_hclk());
    ch32h4_console_puts("\n");

    /* A fault in here is nearly silent -- constructors run before the sketch
     * has done anything, and on this part the console at least exists by now,
     * which is more than the usual Arduino model manages. */
    run_static_constructors();

    /* Published last: in M4 the V3F blocks on this before calling setup1(). */
    ch32h4_runtime_ready = CH32H4_RUNTIME_READY_MAGIC;

    ch32h4_console_puts("V5F: runtime ready\n");

    setup();
    for (;;) {
        loop();
    }
}
