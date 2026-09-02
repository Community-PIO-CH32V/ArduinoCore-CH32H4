/* Self-programming the internal flash.
 *
 * Two things use this: the EEPROM emulation in the flash tail, and LittleFS in
 * the region below it. Both were writing their own copy of the same sequence,
 * which is one copy too many for something that bricks a board when it is
 * wrong.
 *
 * ### Erased flash reads 0xE339E339
 *
 * Not 0xFFFFFFFF. Every "is this blank?" test has to use that constant, and
 * any code carried over from another part decides erased flash is full of
 * data. It is the single most likely thing to get wrong here.
 *
 * ### The erase page is 8 KB on this part, 4 KB on the small one
 *
 * FLASH_CFGR0 bit 28 selects it, and the SDK's FLASH_ErasePage() silently
 * masks the address down accordingly -- so passing it an address in the middle
 * of a page erases the whole page and returns success. Read the size at run
 * time rather than hardcoding 8 KB: the same core builds for the 480 KB part,
 * where the answer is 4 KB and a hardcoded 8 would erase twice what was asked.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* What an erased word reads back as. NOT 0xFFFFFFFF. */
#define CH32H4_FLASH_ERASED_WORD 0xE339E339u

/* 8192 on the 960 KB part, 4096 on the 480 KB one. Erase granularity AND
 * erase alignment: an address anywhere inside a page erases that whole page. */
uint32_t ch32h4_flash_page_size(void);

/* Total user flash, in bytes. */
uint32_t ch32h4_flash_size(void);

/* Erase whole pages. `addr` must be page-aligned and `len` a multiple of the
 * page size; anything else is refused rather than rounded, because rounding
 * an erase outward destroys data the caller never mentioned. */
bool ch32h4_flash_erase(uint32_t addr, uint32_t len);

/* The programming granularity: 256 bytes, a whole page buffer. NOT a word --
 * the SDK's word-at-a-time FLASH_ProgramWord() does not work on this silicon,
 * which is measured and explained in the .c file. */
uint32_t ch32h4_flash_prog_size(void);

/* Program. `addr` and `len` must both be multiples of ch32h4_flash_prog_size()
 * -- the hardware commits a whole page buffer at once and has no partial-page
 * write, so anything else is refused rather than rounded.
 *
 * The target must already be erased. Programming over written flash does not
 * fail; it ANDs, and the result is neither the old value nor the new one. */
bool ch32h4_flash_write(uint32_t addr, const void *src, uint32_t len);

/* Reading is just memory. Here so callers do not have to think about whether
 * it is, and so a future part with a non-mapped region has one place to
 * change. */
void ch32h4_flash_read(uint32_t addr, void *dst, uint32_t len);

/* True if every word in the range reads as erased. */
bool ch32h4_flash_is_erased(uint32_t addr, uint32_t len);

#ifdef __cplusplus
}
#endif
