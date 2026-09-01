#include "Arduino.h"
#include "ch32h4_fault.h"
#include "ch32h4_irq.h"

/* SysTick CTLR bits, reference manual 4.6.1.1. */
#define STK_EN           (1u << 0)
#define STK_IE           (1u << 1)
#define STK_NO_RTC       (1u << 2)
#define STK_AUTO_RELOAD  (1u << 3)

/* Each core has its own SysTick block -- SysTick0 at 0xE000F000 and SysTick1
 * at 0xE000F080 -- but there is only ONE status register, in SysTick0, and
 * core N acknowledges its own bit there. The sketch runs on the V5F, so this
 * uses SysTick1.
 *
 * SysTick counts at HCLK (100 MHz), NOT at the V5F's 400 MHz core clock.
 * Deriving anything here from SystemCoreClock would be a factor-of-four error
 * that no test running on the board could detect, because every part of the
 * board would agree with every other part. The host-side tests measure against
 * the host's clock for exactly this reason.
 */

static volatile uint32_t s_millis = 0;
static uint32_t s_ticks_per_ms = 100000u;
static uint32_t s_ticks_per_us = 100u;

void ch32h4_systick_init(void) {
    const uint32_t hclk = ch32h4_hclk();
    s_ticks_per_ms = hclk / 1000u;
    s_ticks_per_us = hclk / 1000000u;

    SysTick0->CTLR = 0;
    SysTick0->ISR &= ~(1u << 0);
    SysTick0->CNT  = 0;
    SysTick0->CMP  = s_ticks_per_ms - 1u;
    /* STK_EN | STK_IE | STK_NO_RTC | STK_AUTO_RELOAD, per reference manual
     * 4.6.1.1. Only these four bits: the ones above them are not "more of the
     * same" and setting them stops the counter behaving. */
    SysTick0->CTLR = STK_EN | STK_IE | STK_NO_RTC | STK_AUTO_RELOAD;

    NVIC_EnableIRQ(SysTick0_IRQn);
}

/* The attribute goes on the declaration -- see ch32h4_irq.h. On the
 * definition alone it is applied too late, and the vector table quietly gets
 * an ordinary function that returns with `ret` instead of `mret`. */
void CH32H4_IRQ_HANDLER(SysTick0_Handler);
void SysTick0_Handler(void) {
    ch32h4_irq_enter(&ch32h4_irq_systick_count);
    /* Read-modify-write, not a store: the status register holds a bit for
     * each core's timer and this must not clear the other's. */
    SysTick0->ISR &= ~(1u << 0);
    s_millis++;
    ch32h4_irq_exit();
}

__itcm_func unsigned long millis(void) {
    return s_millis;
}

__itcm_func unsigned long micros(void) {
    /* The counter wraps at CMP and raises an interrupt, but the ISR does not
     * run instantly. In the window between the wrap and the ISR, s_millis is
     * one behind what CNT implies -- so a naive read returns
     * (ms)*1000 + ~0 just after having returned (ms)*1000 + 999, and micros()
     * goes backwards. Retrying on `ms != s_millis` does not help, because
     * s_millis has not changed yet.
     *
     * So: mask interrupts briefly, then ask the hardware whether a wrap is
     * pending and fold it in. Masking is what makes this safe rather than
     * merely unlikely -- s_millis cannot move underneath the read. There is no
     * WFI here, so the "WFI with interrupts masked hangs this core" hazard
     * does not apply.
     *
     * Reading the CSR at all requires Machine mode, which is why the startup
     * writes mstatus with MPP=11 rather than WCH's 0x6088. */
    const uint32_t state = __get_MSTATUS();
    __disable_irq();

    uint32_t ms  = s_millis;
    uint32_t cnt = SysTick0->CNT;

    if (SysTick0->ISR & (1u << 0)) {
        /* Wrapped, ISR still pending. Re-read the counter so it is certainly
         * the post-wrap value, and count the tick the ISR has not yet added. */
        cnt = SysTick0->CNT;
        ms += 1u;
    }

    if (state & 0x8u) {   /* MIE was set on entry */
        __enable_irq();
    }

    return (unsigned long)ms * 1000ul + (cnt / s_ticks_per_us);
}

void delay(unsigned long ms) {
    const uint32_t start = s_millis;
    while ((uint32_t)(s_millis - start) < ms) {
        /* Arduino guarantees yield() runs while a sketch waits, and on this
         * core that is where the USB device task lives. A delay() that did not
         * yield would drop the USB connection for its duration. */
        yield();
        /* WFI with interrupts MASKED hangs this core: the RISC-V spec allows
         * resumption on a pending interrupt regardless of mstatus.MIE, and the
         * QingKe does not implement that. Interrupts are enabled here, so this
         * is safe -- and it is why no code in this core ever sleeps inside a
         * critical section. */
        __WFI();
    }
}

__itcm_func void delayMicroseconds(unsigned int us) {
    /* From the raw counter, not micros(): that folds in a missed tick,
     * re-samples until stable and ends in a runtime division -- accurate to a
     * microsecond, and a sizeable fraction of one just to call.
     *
     * The counter wraps at CMP every millisecond, so long waits are counted in
     * whole reload periods rather than by subtracting raw values. */
    if (us == 0) {
        return;
    }
    const uint32_t reload = s_ticks_per_ms;
    uint32_t remaining = us * s_ticks_per_us;
    uint32_t prev = SysTick0->CNT;

    while (remaining > 0) {
        uint32_t now = SysTick0->CNT;
        uint32_t elapsed = (now >= prev) ? (now - prev) : (reload - prev + now);
        if (elapsed >= remaining) {
            return;
        }
        remaining -= elapsed;
        prev = now;
    }
}
