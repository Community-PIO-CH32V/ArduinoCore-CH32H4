/* Committing a new sketch over the running one.
 *
 * THE HARD PART, and it is not the protocol. This code erases and reprograms
 * the flash it is itself stored in. From the moment the first page goes, every
 * instruction it executes -- and every function it calls -- has to come from
 * somewhere other than that array. So this file is ITCM and the flash
 * primitives it calls are ITCM too.
 *
 * That is what ch32h4_flash_ll is for. An earlier version of this file wrote
 * the register sequences out again instead, and got the page-program commit
 * bit wrong: it used the ERASE start, which programs page zero and then fails
 * on every page after it, and looks perfectly successful until something reads
 * the rest back. One copy of the sequences, shared with the ordinary driver.
 *
 * The other core has to be parked as well, in ITCM, for the reason in
 * ch32h4_park.c: a page program does not complete while the V3F is fetching
 * from the array being written. The caller arranges that before entering,
 * because afterwards there is no code left to arrange anything with.
 *
 * THIS FUNCTION DOES NOT RETURN. It resets the part, because the code that
 * called it no longer exists.
 */
#include "ch32h4_ota.h"

#include "ch32h4_flash_ll.h"
#include "ch32h4_itcm.h"
#include "ch32h417.h"

__itcm_func static void reset_now(void) {
    /* PFIC->CFGR = KEY3 | SYSRESET, the sequence startup uses. Written out
     * rather than called, for the reason at the top of the file. */
    PFIC->CFGR = 0xBEEF0000u | (1u << 7);
    for (;;) {
    }
}

__itcm_func void ch32h4_ota_commit(uint32_t dest, const uint8_t *image,
                                   uint32_t len, uint32_t page_size) {
    /* Masked for the whole run, not per page: a handler would vector through
     * a table that is about to stop existing. The primitives mask and restore
     * their own, which nests harmlessly inside this. */
    __asm volatile("csrci mstatus, 8");

    ch32h4_flash_ll_unlock();

    /* Erase everything first, then program. Not interleaved: if power is lost
     * half way, a wholly erased region is at least unambiguous, where a
     * half-written one can look like a valid image. */
    for (uint32_t off = 0; off < len; off += page_size) {
        if (!ch32h4_flash_ll_erase_page(dest + off)) {
            reset_now();
        }
    }

    for (uint32_t off = 0; off < len; off += CH32H4_OTA_PROG_PAGE) {
        /* STRAIGHT FROM THE STAGING BUFFER, with no bounce buffer in between.
         *
         * There used to be one, filled by a word-at-a-time loop, with a comment
         * saying it was written that way rather than memcpy() because memcpy is
         * in flash. GCC recognised the loop and turned it back into a call to
         * memcpy -- a `jalr` from ITCM into the region this function had just
         * erased. It executed 0xE339E339, and the board needed a probe and a
         * `wlink erase` to come back.
         *
         * The bounce buffer bought nothing: the caller's image is already in
         * RAM and word-aligned. Not having it is what makes the loop unable to
         * become a libcall. */
        const uint32_t *src = (const uint32_t *)(const void *)(image + off);
        if (!ch32h4_flash_ll_program_page(dest + off, src)) {
            reset_now();
        }
    }

    ch32h4_flash_ll_lock();
    reset_now();
}
