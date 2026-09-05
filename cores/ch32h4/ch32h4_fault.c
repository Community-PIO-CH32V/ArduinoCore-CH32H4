/* The fault handler, with a UART of its own.
 *
 * A fault during static initialisation is completely silent by default.
 * Constructors run before main(), so before the sketch has called
 * Serial.begin(), and a handler that assumed the console driver had run would
 * print into a UART that was never configured. The board then emits nothing at
 * all -- indistinguishable from a dead chip, a bad flash, or a wedged debug
 * probe, all three of which cost real time to rule out.
 *
 * So this configures the pins and the baud rate from scratch, a dozen register
 * writes, and turns "the board is dead" into a register dump. It deliberately
 * shares nothing with ch32h4_console.c and calls no SDK function: whatever
 * broke may have been in the middle of one.
 */
#include "ch32h417.h"
#include "ch32h4_fault.h"
#include "ch32h4_irq.h"
#include "ch32h4_xcore.h"

/* The fault record itself, in shared RAM. See ch32h4_fault.h. */
volatile ch32h4_fault_log_t ch32h4_fault_log CH32H4_XCORE;
volatile ch32h4_fault_log_t ch32h4_fault_log_v3f CH32H4_XCORE;

/* Interrupt trace. Nesting is tracked by a balanced +/- in each handler body:
 * an interrupt landing on top of another sees the outer one's + not yet undone,
 * so the count is the true C-level nesting depth.
 *
 * These live in .xcore (shared, NOLOAD, not cleared by the V3F's xcore_init)
 * so they survive the reset that follows a fault: the fault handler's own
 * entry usually cannot run when the stack is already gone, so it cannot record
 * them itself. The V3F prints and resets them on the next boot. */
volatile uint32_t ch32h4_irq_eth_count      CH32H4_XCORE;
volatile uint32_t ch32h4_irq_systick_count  CH32H4_XCORE;
volatile uint32_t ch32h4_eth_last_frame     CH32H4_XCORE;
volatile uint32_t ch32h4_eth_last_tx        CH32H4_XCORE;
volatile uint32_t ch32h4_eth_phase          CH32H4_XCORE;
volatile uint32_t ch32h4_irq_usbfs_count    CH32H4_XCORE;
volatile uint32_t ch32h4_irq_usart1_count   CH32H4_XCORE;
volatile uint32_t ch32h4_irq_max_nesting    CH32H4_XCORE;
volatile uint32_t ch32h4_irq_nesting        CH32H4_XCORE;
volatile uint32_t ch32h4_trace_magic        CH32H4_XCORE;

void ch32h4_irq_enter(volatile uint32_t *counter) {
    (*counter)++;
    uint32_t d = ++ch32h4_irq_nesting;
    if (d > ch32h4_irq_max_nesting) {
        ch32h4_irq_max_nesting = d;
    }
}

void ch32h4_irq_exit(void) {
    ch32h4_irq_nesting--;
}

/* Reprogramming a peripheral the console may already own is fine here: this
 * function never returns, so there is nothing left to interfere with. */
static void raw_uart_bringup(void) {
    /* If the console is already up, leave it alone. Reprogramming a working
     * USART mid-stream drops characters and mangles the dump -- and the dump
     * is the entire point. Only build it from nothing when there is nothing,
     * which is the static-init case this function exists for. */
    if (USART1->CTLR1 & USART_CTLR1_UE) {
        return;
    }

    RCC->HB2PCENR |= RCC_HB2Periph_GPIOA | RCC_HB2Periph_USART1
                     | RCC_HB2Periph_AFIO;
    (void)RCC->HB2PCENR;

    /* PA9 -> AF7. The AF mux has a low and a high register per port; PA9 is
     * nibble 1 of GPIOA_AFHR. Written directly rather than through
     * GPIO_PinAFConfig, because whatever faulted may have been inside the SDK
     * and this handler must not depend on it. */
    AFIO->GPIOA_AFHR = (AFIO->GPIOA_AFHR & ~(0xFu << 4)) | (7u << 4);

    /* PA9 as AF push-pull, 50 MHz: nibble 0xB in CFGHR's PA9 field. */
    GPIOA->CFGHR = (GPIOA->CFGHR & ~(0xFu << 4)) | (0xBu << 4);

    /* HCLK is 100 MHz on a good boot; if the clock tree never came up this
     * prints at the wrong rate, which is still more than silence. The BRR
     * format is 12.4 fixed point: HCLK/baud. */
    USART1->BRR = (uint16_t)(((100000000u / 16u) * 16u + 115200u / 2u) / 115200u);
    USART1->CTLR1 = USART_CTLR1_UE | USART_CTLR1_TE;
    USART1->CTLR2 = 0;
    USART1->CTLR3 = 0;
}

static void putc_raw(char c) {
    uint32_t guard = 1000000u;
    while (!(USART1->STATR & USART_STATR_TXE) && --guard) {
    }
    USART1->DATAR = (uint16_t)(uint8_t)c;
}

static void puts_raw(const char *s) {
    for (; *s; s++) {
        if (*s == '\n') {
            putc_raw('\r');
        }
        putc_raw(*s);
    }
}

static void puthex_raw(uint32_t v) {
    static const char digits[] = "0123456789abcdef";
    puts_raw("0x");
    for (int i = 28; i >= 0; i -= 4) {
        putc_raw(digits[(v >> i) & 0xFu]);
    }
}

/* The fault handler runs on a PRIVATE stack, not the one the fault just
 * overflowed. The "WCH-Interrupt-fast" prologue saves 80 bytes of FPU state to
 * the CURRENT sp -- exactly what a stack overflow has destroyed -- so the
 * naked entry below switches to this fixed buffer before any C code runs, and
 * hands the original sp to fault_dump() for the call-chain walk. */
/* __attribute__((used)) on all three, and on ch32h4_fault_log where it is
 * defined: HardFault_Handler below is the only thing that names them, and it
 * names them inside an inline-asm string. The compiler does not parse that
 * string, so under -flto it sees three globals nothing references, drops them,
 * and the link fails with "undefined reference to ch32h4_fault_sp" pointing at
 * an <artificial> file. Without LTO it happens to work, because the symbols
 * survive to the assembler within their own translation unit -- which is why
 * this is exactly the kind of thing that only breaks when LTO is turned on. */
uint8_t ch32h4_fault_stack[512] __attribute__((aligned(16), used));
uint32_t ch32h4_fault_sp __attribute__((used));

__attribute__((used))
void ch32h4_fault_dump(void) {
    __disable_irq();

    /* Finish the record the naked entry started. It could write the CSRs and
     * nothing else -- it runs with no usable stack and no guarantee gp is
     * intact -- so the interrupt trace is copied here, where ordinary loads
     * and stores are available again. Leaving these fields alone was worth a
     * replayed post-mortem full of eight-digit garbage that read exactly like
     * an interrupt storm. */
    ch32h4_fault_log.irq_eth         = ch32h4_irq_eth_count;
    ch32h4_fault_log.irq_systick     = ch32h4_irq_systick_count;
    ch32h4_fault_log.irq_usbfs       = ch32h4_irq_usbfs_count;
    ch32h4_fault_log.irq_usart1      = ch32h4_irq_usart1_count;
    ch32h4_fault_log.irq_max_nesting = ch32h4_irq_max_nesting;

    /* Printed from the record, not re-read from the CSRs. __disable_irq() above
     * has already changed mstatus, so a fresh read reports an mstatus the fault
     * never had -- and the replayed record and the live dump then disagree
     * about the same fault. */
    const uint32_t mcause  = ch32h4_fault_log.mcause;
    const uint32_t mepc    = ch32h4_fault_log.mepc;
    const uint32_t mtval   = ch32h4_fault_log.mtval;
    const uint32_t mstatus = ch32h4_fault_log.mstatus;

    raw_uart_bringup();

    puts_raw("\n\n=== TRAP ===\nmcause=");
    puthex_raw(mcause);
    puts_raw(" mepc=");
    puthex_raw(mepc);
    puts_raw(" mtval=");
    puthex_raw(mtval);
    puts_raw(" mstatus=");
    puthex_raw(mstatus);
    puts_raw("\nsp=");
    puthex_raw(ch32h4_fault_sp);

    puts_raw("\nstack:");
    for (uint32_t i = 0; i < 24; i++) {
        uint32_t w = ((volatile uint32_t *)ch32h4_fault_sp)[i];
        /* No lower bound on the first range: w is unsigned, so >= 0 is
           always true and -Wtype-limits says so. The three ranges are the
           flash alias at zero, the flash proper, and RAM. */
        if (w < 0x00100000u
            || (w >= 0x08000000u && w < 0x08100000u)
            || (w >= 0x20000000u && w < 0x20180000u)) {
            puts_raw(" ");
            puthex_raw(w);
        }
    }
    puts_raw("\n");

    if ((mcause & 0x7FFFFFFFu) == 2u) {
        puts_raw("illegal instruction\n");
    }

    /* Let the last characters clear the shift register. NVIC_SystemReset()
     * takes effect immediately, and a reset mid-frame truncates the very line
     * that says what went wrong. */
    for (volatile uint32_t i = 0; i < 200000u; i++) {
    }
    NVIC_SystemReset();
}

/* Naked: no prologue, so nothing is written to the stack the fault may have
 * destroyed. Three things happen, in an order chosen so each still happens if
 * the next cannot:
 *
 *   1. Record the CSRs into the shared fault log. PC-relative, no gp, no C, no
 *      stack -- so it survives a stack that has run off the end of its region.
 *      The V3F prints the record on the next boot.
 *   2. Switch sp to a private buffer and hand the faulting sp to the C dumper,
 *      which prints "=== TRAP ===" now rather than one reset later. It needs
 *      its own stack because the "WCH-Interrupt-fast" prologue saves 80 bytes
 *      of FPU state to the CURRENT sp -- exactly what an overflow destroyed.
 *   3. ch32h4_fault_dump() resets. Spinning here instead wedges the probe with
 *      a 0x55 protocol error, which needs NRST held down through a flash to
 *      recover, so a fault must never end in a loop. */
__attribute__((naked))
void HardFault_Handler(void) {
    __asm volatile(
        "la   t0, ch32h4_fault_log   ;"
        "csrr t1, mcause             ;"
        "sw   t1, 4(t0)              ;"
        "csrr t1, mepc               ;"
        "sw   t1, 8(t0)              ;"
        "csrr t1, mtval              ;"
        "sw   t1, 12(t0)             ;"
        "csrr t1, mstatus            ;"
        "sw   t1, 16(t0)             ;"
        "sw   sp, 20(t0)             ;"
        "li   t1, %0                 ;"
        "sw   t1, 0(t0)              ;"
        "la   t0, ch32h4_fault_sp    ;"
        "sw   sp, 0(t0)              ;"
        "la   sp, ch32h4_fault_stack ;"
        "addi sp, sp, %1             ;"
        "j    ch32h4_fault_dump      ;"
        :: "i"(CH32H4_FAULT_LOG_MAGIC), "i"(sizeof(ch32h4_fault_stack)));
}
