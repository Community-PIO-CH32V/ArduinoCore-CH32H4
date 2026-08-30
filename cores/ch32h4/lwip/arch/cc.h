/* lwIP's compiler/platform layer.
 *
 * Deliberately minimal. lwIP's own headers supply the integer types from
 * <stdint.h>, so all that is left is what it cannot infer.
 */
#pragma once

#include <assert.h>

/* Diagnostics are compiled out.
 *
 * lwIP's debug output goes through printf, and on this core printf reaches
 * whatever `Serial` is -- which is a USB CDC endpoint. An assert firing inside
 * the stack, potentially from the Ethernet interrupt, would then try to push a
 * string through TinyUSB from interrupt context while the main loop may be
 * inside the same code. Silence is the safer default; point these at
 * ch32h4_console_puts (a raw, unbuffered UART) when chasing a stack bug,
 * because that one is safe to call from anywhere. */
#define LWIP_PLATFORM_DIAG(x)
#define LWIP_PLATFORM_ASSERT(x)   do { } while (0)

/* lwIP's own ctype is smaller than newlib's and drags in no locale tables. */
#define LWIP_NO_CTYPE_H 1
