/* How to declare an interrupt handler on this part.
 *
 * The attribute MUST be on a DECLARATION that GCC sees before the definition.
 * Put it on the definition alone and GCC has already emitted the prologue by
 * the time it reads the attribute, so it silently gives you an ordinary
 * function -- one that ends in `ret` instead of `mret` -- and puts it in the
 * vector table anyway. There is no warning. The first interrupt then returns
 * into nowhere, and on this part that presents as a reset loop, not a fault.
 *
 * Declare handlers as:
 *
 *     void CH32H4_IRQ_HANDLER(SysTick1_Handler);
 *     void SysTick1_Handler(void) { ... }
 *
 * WCH's GCC takes an argument selecting its fast interrupt entry, which uses
 * the custom `xw` instructions and the hardware stack push. A stock RISC-V GCC
 * has never heard of that string and rejects it, so a generic toolchain falls
 * back to the standard attribute: a slower prologue, but one that compiles.
 */
#pragma once

#if defined(__riscv) && defined(CH32H4_TOOLCHAIN_GENERIC)
#define CH32H4_IRQ_HANDLER(name)  name(void) __attribute__((interrupt))
#else
#define CH32H4_IRQ_HANDLER(name)  name(void) __attribute__((interrupt("WCH-Interrupt-fast")))
#endif
