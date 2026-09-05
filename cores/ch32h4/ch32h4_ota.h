/* Committing a staged image over the running sketch.
 *
 * The last step of an over-the-air update, and the only part of it that has to
 * be in the core rather than a library: it erases and reprograms the flash it
 * is stored in, so it lives entirely in ITCM and calls nothing outside itself.
 * See ch32h4_ota.c.
 *
 * DOES NOT RETURN. It resets the part -- the code that called it no longer
 * exists by then.
 *
 * The caller must have:
 *   - the whole image in RAM, word-aligned, and verified. Once this starts
 *     there is no going back and nothing left to check with.
 *   - parked the other core: ch32h4_park_other(). A page program does not
 *     complete while the V3F is fetching from the array being written.
 *   - stopped anything that takes interrupts and matters. This masks them,
 *     but a half-finished DMA into a buffer is still a half-finished DMA.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#include "ch32h4_flash_ll.h"

/* The programming granularity. `len` must be a multiple of this AND of the
 * erase page size passed to commit(). */
#define CH32H4_OTA_PROG_PAGE  CH32H4_FLASH_LL_PROG_PAGE

void ch32h4_ota_commit(uint32_t dest, const uint8_t *image, uint32_t len,
                       uint32_t page_size);

#ifdef __cplusplus
}
#endif
