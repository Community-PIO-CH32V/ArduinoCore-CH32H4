/* The early console: raw USART1, no buffering, no interrupts.
 *
 * This exists so the board can speak before anything else works. A fault
 * during static initialisation is otherwise completely silent -- constructors
 * run before main(), so before Serial exists, and the board emits nothing at
 * all, which is indistinguishable from a dead chip, a bad flash or a wedged
 * debug probe.
 *
 * HardwareSerial (Serial1) takes over the same peripheral later, with an
 * interrupt-driven RX ring. These functions keep working afterwards, because
 * they only touch TXE and DATAR.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* PA9 TX / PA10 RX, AF7. Safe to call more than once. */
void ch32h4_console_init(uint32_t baud);

void ch32h4_console_putc(char c);

/* Translates '\n' to "\r\n". */
void ch32h4_console_puts(const char *s);

/* Unsigned decimal. There is no printf here on purpose: printf allocates its
 * stdout buffer from the heap on first use, and this has to work before the
 * heap is trustworthy. */
void ch32h4_console_putu(uint32_t v);

/* Zero-padded 32-bit hex, "0x" prefixed. */
void ch32h4_console_puthex(uint32_t v);

/* Wait until the last byte has actually left the shift register.
 *
 * putc only waits for TXE -- room in the holding register -- so on return from
 * a print up to two bytes are still on the wire. If the next thing that
 * happens is a trap or a reset, those bytes are lost and the line arrives
 * truncated, which reads exactly like the board dying earlier than it did.
 * That cost a wrong diagnosis once: "mtvec=0" was really "mtvec=0x..." cut
 * after the first character. */
void ch32h4_console_flush(void);

#ifdef __cplusplus
}
#endif
