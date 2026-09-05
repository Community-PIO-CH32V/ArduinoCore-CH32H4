/* What the V3F does.
 *
 * It boots first, brings the part up, hands control to the V5F and sleeps.
 * In M4 the sleep is replaced by setup1()/loop1().
 */
#include "ch32h4_clock.h"
#include "ch32h4_console.h"
#include "ch32h4_fault.h"
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

/* Print why the part reset. A lockup (a fault inside the fault handler) and an
 * independent-watchdog timeout both present as a clean reboot with no "TRAP"
 * line, and nothing downstream can tell the two apart -- so they are decoded
 * here, at the first moment the console can speak. */
CH32H4_V3F_TEXT
static void print_reset_cause(uint32_t rst) {
    uint32_t flags = rst & 0xFC000000u;   /* RSTSCKR[31:26] */
    ch32h4_console_puts("rst=");
    ch32h4_console_puthex(flags);
    if (flags & RCC_LOCKUPRSTF) ch32h4_console_puts(" lockup");
    if (flags & RCC_WWDGRSTF)   ch32h4_console_puts(" wwdg");
    if (flags & RCC_IWDGRSTF)   ch32h4_console_puts(" iwdg");
    if (flags & RCC_SFTRSTF)    ch32h4_console_puts(" soft");
    if (flags & RCC_PORRSTF)    ch32h4_console_puts(" por");
    if (flags & RCC_PINRSTF)    ch32h4_console_puts(" pin");
    if (flags == 0)             ch32h4_console_puts(" none");
    ch32h4_console_puts("\n");
    RCC->RSTSCKR |= RCC_RMVF;          /* arm for the next reset */
}

CH32H4_V3F_TEXT
void ch32h4_v3f_main(void) {
    /* Read the reset cause before anything (SystemInit, drivers) can disturb
     * the latch. */
    uint32_t reset_cause = RCC->RSTSCKR;

    vio18_init();
    ch32h4_clock_init();
    ch32h4_console_init(115200);

    ch32h4_console_puts("\nCH32H4 Arduino core\n");
    print_reset_cause(reset_cause);

    /* Everything below reads .xcore, which is NOLOAD. On a cold power-up it
     * holds whatever the SRAM came up with, and printing that gives a boot
     * report of eight-digit nonsense indistinguishable from a real crash --
     * which is how a power-cycle comes to look like a firmware bug. The magic
     * word says the region has been through here at least once. */
    if (ch32h4_trace_magic != CH32H4_TRACE_MAGIC) {
        ch32h4_trace_magic           = CH32H4_TRACE_MAGIC;
        ch32h4_fault_log.magic       = 0;
        ch32h4_fault_log.boot_faults = 0;
        ch32h4_fault_log_v3f.magic   = 0;
        ch32h4_irq_eth_count     = 0;
        ch32h4_irq_systick_count = 0;
        ch32h4_irq_usbfs_count   = 0;
        ch32h4_irq_usart1_count  = 0;
        ch32h4_irq_max_nesting   = 0;
        ch32h4_irq_nesting       = 0;
    }

    /* A magic word cannot police this region on its own. .xcore is NOLOAD and
     * survives a reflash, so a new build whose variables sit at different
     * offsets reads the old image's bytes through the new layout and finds the
     * magic exactly where it expects it -- which is how boot_faults came to
     * report 577168213 crashes in a row. Everything read out of here is
     * therefore also range-checked against what it can legitimately be. */
    if (ch32h4_fault_log.boot_faults > CH32H4_FAULT_REBOOT_LIMIT) {
        ch32h4_fault_log.boot_faults = 0;
    }

    /* A lockup leaves no record at all -- it is a fault taken inside the fault
     * handler, so the handler never gets to write one. It is also the failure
     * most likely to be reproducible on every boot, and therefore the one most
     * likely to reset the part faster than a probe can attach. Count it from
     * the reset cause instead. */
    if (reset_cause & RCC_LOCKUPRSTF) {
        ch32h4_fault_log.boot_faults++;
    }

    /* This core's own trap record, from Stray_IRQ_v3f. An interrupt with no
     * handler used to jump to address 0 -- which is _start_v3f -- and silently
     * re-run startup, so the console showed a clean reboot and nothing else. */
    if (ch32h4_fault_log_v3f.magic == CH32H4_FAULT_LOG_MAGIC) {
        ch32h4_console_puts("v3f trap: mcause=");
        ch32h4_console_puthex(ch32h4_fault_log_v3f.mcause);
        ch32h4_console_puts(" mepc=");
        ch32h4_console_puthex(ch32h4_fault_log_v3f.mepc);
        ch32h4_console_puts(" mtval=");
        ch32h4_console_puthex(ch32h4_fault_log_v3f.mtval);
        ch32h4_console_puts(" mstatus=");
        ch32h4_console_puthex(ch32h4_fault_log_v3f.mstatus);
        ch32h4_console_puts(" sp=");
        ch32h4_console_puthex(ch32h4_fault_log_v3f.sp);
        ch32h4_console_puts(" ra=");
        ch32h4_console_puthex(ch32h4_fault_log_v3f.irq_eth);
        ch32h4_console_puts(" stack=");
        ch32h4_console_puthex(ch32h4_fault_log_v3f.irq_systick);
        ch32h4_console_putc(',');
        ch32h4_console_puthex(ch32h4_fault_log_v3f.irq_usbfs);
        ch32h4_console_putc(',');
        ch32h4_console_puthex(ch32h4_fault_log_v3f.irq_usart1);
        ch32h4_console_putc(',');
        ch32h4_console_puthex(ch32h4_fault_log_v3f.irq_max_nesting);
        ch32h4_console_puts("\n");
        ch32h4_console_flush();
        ch32h4_fault_log_v3f.magic = 0;
        ch32h4_fault_log.boot_faults++;
    }

    /* If the V5F faulted last run, and the fault was bad enough that the fault
     * handler itself faulted (a lockup), the only trace is this record. Print
     * it now, before waking the V5F again, and clear it for next time. */
    if (ch32h4_fault_log.magic == CH32H4_FAULT_LOG_MAGIC) {
        ch32h4_console_puts("v5f fault: mcause=");
        ch32h4_console_puthex(ch32h4_fault_log.mcause);
        ch32h4_console_puts(" mepc=");
        ch32h4_console_puthex(ch32h4_fault_log.mepc);
        ch32h4_console_puts(" mtval=");
        ch32h4_console_puthex(ch32h4_fault_log.mtval);
        ch32h4_console_puts(" mstatus=");
        ch32h4_console_puthex(ch32h4_fault_log.mstatus);
        ch32h4_console_puts(" sp=");
        ch32h4_console_puthex(ch32h4_fault_log.sp);
        ch32h4_console_puts("\n");
        ch32h4_console_puts("irq eth=");
        ch32h4_console_putu(ch32h4_fault_log.irq_eth);
        ch32h4_console_puts(" systick=");
        ch32h4_console_putu(ch32h4_fault_log.irq_systick);
        ch32h4_console_puts(" usbfs=");
        ch32h4_console_putu(ch32h4_fault_log.irq_usbfs);
        ch32h4_console_puts(" usart1=");
        ch32h4_console_putu(ch32h4_fault_log.irq_usart1);
        ch32h4_console_puts(" max_nesting=");
        ch32h4_console_putu(ch32h4_fault_log.irq_max_nesting);
        ch32h4_console_puts(" boot_faults=");
        ch32h4_console_putu(ch32h4_fault_log.boot_faults + 1u);
        ch32h4_console_puts("\n");
        ch32h4_console_flush();
        ch32h4_fault_log.magic = 0;
        ch32h4_fault_log.boot_faults++;
    }

    /* The interrupt trace, from the V5F's last run. On a lockup the fault
     * handler never runs (its own entry needs the stack that is already gone),
     * so the counters are what say which interrupt was storming. Read them
     * before the wake, then reset them for the coming run. Nothing is printed
     * when the V5F never got as far as its first tick -- an all-zero line
     * every boot is noise that trains you to skip the one that matters. */
    if (ch32h4_irq_systick_count != 0) {
        ch32h4_console_puts("irq eth=");
        ch32h4_console_putu(ch32h4_irq_eth_count);
        ch32h4_console_puts(" systick=");
        ch32h4_console_putu(ch32h4_irq_systick_count);
        ch32h4_console_puts(" usbfs=");
        ch32h4_console_putu(ch32h4_irq_usbfs_count);
        ch32h4_console_puts(" usart1=");
        ch32h4_console_putu(ch32h4_irq_usart1_count);
        ch32h4_console_puts(" max_nesting=");
        ch32h4_console_putu(ch32h4_irq_max_nesting);
        ch32h4_console_puts("\n");
    }

    ch32h4_irq_eth_count     = 0;
    ch32h4_irq_systick_count = 0;
    ch32h4_irq_usbfs_count   = 0;
    ch32h4_irq_usart1_count  = 0;
    ch32h4_irq_max_nesting   = 0;
    ch32h4_irq_nesting       = 0;

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

    /* Stop relaunching a core that faults on every boot.
     *
     * The fault handler resets rather than spinning, because a core
     * spinning with interrupts off wedges the WCH-Link with a 0x55
     * protocol error that only NRST-held-through-an-erase clears. But a
     * fault that reproduces every time then resets several times a second,
     * and a part in that state is exactly as unreachable -- the probe never
     * gets a window in which to attach.
     *
     * So after CH32H4_FAULT_REBOOT_LIMIT of them this core keeps the
     * console and leaves the V5F asleep. The board stays talkable and
     * reflashable, and the record above says what went wrong. The V5F
     * clears the count itself once it reaches runtime-ready. */
    if (ch32h4_fault_log.boot_faults >= CH32H4_FAULT_REBOOT_LIMIT) {
        ch32h4_console_puts("V3F: the V5F has faulted ");
        ch32h4_console_putu(ch32h4_fault_log.boot_faults);
        ch32h4_console_puts(" boots running. NOT waking it.\n");
        ch32h4_console_puts("V3F: reflash, or reset twice to try again.\n");
        ch32h4_console_flush();
        /* Two resets clears it: this store lands before the next boot
         * reads it, so a plain reset here is the escape hatch. */
        ch32h4_fault_log.boot_faults = 0;
        for (;;) {
        }
    }

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
