/* State shared between the two cores.
 *
 * Anything here must live in XCORE_RAM, in the shared region: it is the only
 * memory both cores reach at speed, and the V5F's I-cache is not coherent with
 * anything else. It also cannot live in .bss, because the V3F zeroes .bss
 * before the V5F is awake.
 *
 * The section is NOLOAD, so nothing initialises it at reset -- the V3F clears
 * what it needs explicitly before waking the V5F.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CH32H4_XCORE  __attribute__((section(".xcore")))

/* A magic value rather than 1, because the region is uninitialised at reset
 * and a stale or random word must not read as "ready". */
#define CH32H4_RUNTIME_READY_MAGIC  0x5F5FA5A5u

/* Set by the V5F once .init_array has run and the C++ runtime is usable.
 * In M4 the V3F waits for this before calling setup1(). */
extern volatile uint32_t ch32h4_runtime_ready;

#ifdef __cplusplus
}
#endif
