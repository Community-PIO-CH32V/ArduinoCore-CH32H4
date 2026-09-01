/* The hardware true random number generator.
 *
 * Whitened. Raw words from this peripheral are NOT uniformly distributed and
 * must never be handed out directly -- see the measurements in ch32h4_rng.c.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* True if the generator is running and has not reported a clock error. The
 * only failure that cannot be recovered from in software is a clock out of
 * range, so a caller that needs to know can ask rather than silently accepting
 * numbers of unknown quality. */
bool ch32h4_rng_ok(void);

/* A whitened 32-bit value. Returns 0 only if the peripheral never becomes
 * ready, which ch32h4_rng_ok() then reports as false. */
uint32_t ch32h4_rng_u32(void);
uint64_t ch32h4_rng_u64(void);

/* n whitened bytes. The destination needs no alignment. */
void ch32h4_rng_bytes(void *buf, size_t n);

/* One raw word, unwhitened, for diagnostics only. Exposed so a test can
 * measure the source's distribution and demonstrate why the whitening above
 * is not optional. Do not use it for anything else. */
uint32_t ch32h4_rng_raw_unsafe(void);

#ifdef __cplusplus
}
#endif
