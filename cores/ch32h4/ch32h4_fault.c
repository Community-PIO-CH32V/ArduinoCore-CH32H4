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
#include "ch32h4_irq.h"

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

/* Overrides the weak HardFault_Handler in startup_v5f.S. The attribute is on
 * the declaration -- see ch32h4_irq.h. */
void CH32H4_IRQ_HANDLER(HardFault_Handler);
void HardFault_Handler(void) {
    /* Stop the world. Without this SysTick keeps firing into the spin loop at
     * the bottom, each one nesting on the last until the hardware stack
     * overflows and the part resets -- so the dump scrolls past and the board
     * appears to boot-loop instead of halting. */
    __disable_irq();

    uint32_t mcause, mepc, mtval, mstatus;
    __asm volatile("csrr %0, mcause"  : "=r"(mcause));
    __asm volatile("csrr %0, mepc"    : "=r"(mepc));
    __asm volatile("csrr %0, mtval"   : "=r"(mtval));
    __asm volatile("csrr %0, mstatus" : "=r"(mstatus));

    raw_uart_bringup();

    puts_raw("\n\n=== TRAP ===\nmcause=");
    puthex_raw(mcause);
    puts_raw(" mepc=");
    puthex_raw(mepc);
    puts_raw(" mtval=");
    puthex_raw(mtval);
    puts_raw(" mstatus=");
    puthex_raw(mstatus);
    puts_raw("\n");

    /* mcause 2 is an illegal instruction, which on this part most often means
     * an M-mode CSR was touched from User mode -- see docs/hazards.md. */
    if ((mcause & 0x7FFFFFFFu) == 2u) {
        puts_raw("illegal instruction: an M-mode CSR from User mode?\n");
    }

    puts_raw("halted\n");
    for (;;) {
    }
}
