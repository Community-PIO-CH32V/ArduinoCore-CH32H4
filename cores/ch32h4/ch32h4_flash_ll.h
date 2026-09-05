/* The flash controller, at the register level, and in ITCM.
 *
 * ONE COPY OF THE SEQUENCES. There were three: this driver's, the vendor
 * SDK's, and a hand-written one in the OTA committer -- and the third had a
 * bug the other two did not, because it was re-derived rather than reused. It
 * used the ERASE start bit to commit a page program, which programs the first
 * page and then quietly fails. Everything that talks to the flash controller
 * now goes through here.
 *
 * ALL OF IT IS __itcm_func, and that is the point. The OTA committer erases
 * the flash it is running from, so every instruction it executes -- including
 * every function it calls -- has to come from somewhere else. Putting the
 * primitives in ITCM is what lets the ordinary driver and the committer share
 * them: the driver does not need it, and paying for it costs nothing.
 *
 * It also fixes something quieter. The old erase path called the SDK's
 * FLASH_ErasePage(), which is flash-resident, so the core was executing from
 * the array it was erasing and got away with it only because the V5F's
 * instruction cache happened to hold the loop. That is not a guarantee.
 *
 * INTERRUPTS ARE MASKED inside each operation and restored after, so a caller
 * doing one page at a time still services interrupts between pages. A caller
 * that must not be interrupted at all -- the committer -- masks them itself
 * around the whole run; nesting is harmless.
 *
 * WHAT IS NOT HERE: parking the other core. That is the caller's, because the
 * right granularity differs -- ch32h4_flash_write() parks once around a whole
 * buffer, and the committer parks once around everything.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* The programming granularity: a whole page buffer, not a word. The SDK's
 * word-at-a-time FLASH_ProgramWord() does not work on this silicon. */
#define CH32H4_FLASH_LL_PROG_PAGE  256u

/* What an erased word reads back as. NOT 0xFFFFFFFF. */
#define CH32H4_FLASH_LL_ERASED     0xE339E339u

/* Fast-mode unlock. Both key pairs: KEYR alone leaves fast programming
 * locked, and the page write then does nothing and reports success. */
void ch32h4_flash_ll_unlock(void);
void ch32h4_flash_ll_lock(void);

/* Erase granularity AND alignment: 8192 on the 960 KB part, 4096 on the
 * 480 KB one. An address anywhere inside a page erases that whole page. */
uint32_t ch32h4_flash_ll_page_size(void);

/* One page. The address must be page-aligned; this does not mask it down, so
 * that a caller cannot erase a page it did not mean to. */
bool ch32h4_flash_ll_erase_page(uint32_t addr);

/* One 256-byte page from a word-aligned source. */
bool ch32h4_flash_ll_program_page(uint32_t addr, const uint32_t *src);

#ifdef __cplusplus
}
#endif
