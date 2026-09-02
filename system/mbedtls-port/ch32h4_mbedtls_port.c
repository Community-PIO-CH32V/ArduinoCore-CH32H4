/* The two things mbedTLS needs from this board: entropy, and a clock.
 *
 * Everything else it does in software or on the ECDC block (see aes_alt.c).
 */
#include <stddef.h>
#include <stdint.h>

#include "mbedtls/build_info.h"

#include "Arduino.h"
#include "ch32h4_rng.h"

/* The entropy source, and the only thing standing between TLS and predictable
 * keys. MBEDTLS_NO_PLATFORM_ENTROPY and MBEDTLS_ENTROPY_HARDWARE_ALT are both
 * set, so mbedtls has no other.
 *
 * It goes through ch32h4_rng_bytes() rather than reading the RNG data register
 * directly, and that matters more than it looks: raw, this peripheral yields
 * only about ten bits of entropy per 32-bit word -- measured, roughly 470
 * distinct values in 600 reads -- while reporting no error of any kind.
 * ch32h4_rng.c conditions it; the measurements are there.
 *
 * mbedtls then runs what it gets through its own entropy accumulator and
 * CTR_DRBG, so the conditioning here is the second of two, not the only one.
 * That is the right arrangement: a hardware source is a noise source, not a
 * generator. */
int mbedtls_hardware_poll(void *data, unsigned char *output, size_t len,
                          size_t *olen) {
    (void)data;
    if (!output || !olen) {
        return -1;
    }
    ch32h4_rng_bytes(output, len);
    *olen = len;
    return 0;
}

#if defined(MBEDTLS_HAVE_TIME)
#include "mbedtls/platform_time.h"

/* Monotonic milliseconds, for mbedtls's interval timers.
 *
 * Deliberately NOT derived from the RTC. This is used for handshake and
 * retransmission timeouts, and it must not jump when the wall clock is set --
 * which, on a board that syncs from SNTP during startup, it will, by decades.
 *
 * mbedtls_time() itself is not defined here: it defaults to the C library's
 * time(), which the core backs with the RTC (see ch32h4_rtc.c), so
 * certificate validity is checked against the real clock. */
mbedtls_ms_time_t mbedtls_ms_time(void) {
    return (mbedtls_ms_time_t)millis();
}
#endif
