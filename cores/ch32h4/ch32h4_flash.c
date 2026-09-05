#include "ch32h4_flash.h"

#include "Arduino.h"
#include "ch32h4_itcm.h"
#include "ch32h4_park.h"

#include <string.h>

/* The capacity bit the SDK's own FLASH_ErasePage() consults to decide whether
 * to mask the address to 8 KB or 4 KB. Reading the same bit is what keeps this
 * driver and that one from disagreeing about where a page begins. */
#define FLASH_CAPACITY_IS_960K  (((*(volatile uint32_t *)FLASH_CFGR0_BASE) \
                                  & (1u << 28)) != 0u)

uint32_t ch32h4_flash_page_size(void) {
    return FLASH_CAPACITY_IS_960K ? 8192u : 4096u;
}

uint32_t ch32h4_flash_size(void) {
    return FLASH_CAPACITY_IS_960K ? (960u * 1024u) : (480u * 1024u);
}

void ch32h4_flash_read(uint32_t addr, void *dst, uint32_t len) {
    memcpy(dst, (const void *)(uintptr_t)addr, len);
}

bool ch32h4_flash_is_erased(uint32_t addr, uint32_t len) {
    if ((addr & 3u) || (len & 3u)) {
        return false;
    }
    const volatile uint32_t *p = (const volatile uint32_t *)(uintptr_t)addr;
    for (uint32_t i = 0; i < len / 4u; i++) {
        if (p[i] != CH32H4_FLASH_ERASED_WORD) {
            return false;
        }
    }
    return true;
}

/* How long to wait for the other core to park.
 *
 * It parks from its main loop and from yield(), so a loop1() that returns or
 * delays is in within a millisecond or two. A hundred is generous enough to
 * cover a slow iteration and short enough that a sketch which is never going
 * to park gets its answer rather than waiting. */
#define PARK_TIMEOUT_MS  100u

bool ch32h4_flash_erase(uint32_t addr, uint32_t len) {
    const uint32_t page = ch32h4_flash_page_size();

    /* Refused rather than rounded. FLASH_ErasePage() masks the address down to
     * the page it lands in, so an unaligned call quietly erases from the start
     * of a page the caller did not ask about -- and if that page holds the
     * sketch, the board does not come back. */
    if (len == 0u || (addr % page) != 0u || (len % page) != 0u) {
        return false;
    }

    /* The other core must not be fetching from flash while this runs. See
     * ch32h4_park.c: a page program with the V3F running does not complete,
     * and the board hangs. Refusing is the only safe answer when it will not
     * park -- a caller gets a failed write it can see. */
    if (!ch32h4_park_other(PARK_TIMEOUT_MS)) {
        return false;
    }

    FLASH_Unlock();
    bool ok = true;
    for (uint32_t off = 0; off < len; off += page) {
        if (FLASH_ErasePage(addr + off) != FLASH_COMPLETE) {
            ok = false;
            break;
        }
    }
    FLASH_Lock();
    ch32h4_unpark_other();
    return ok;
}

/* ---- programming ---------------------------------------------------------
 *
 * This part programs a 256-BYTE PAGE at a time, not a word.
 *
 * The SDK offers FLASH_ProgramWord(), which writes two half-words in the
 * standard mode, and it does not work here: after an erase, programming 16
 * bytes with it leaves the first word correct and the rest wrong, and the
 * status register reports failure. Measured -- erase and blank-check both
 * pass, the erased word reads 0xE339E339 as documented, and only the program
 * fails.
 *
 * The working path is the fast page program: unlock both the FPEC and the
 * fast-mode key, set CR_PAGE_PG, write 64 words into the page buffer, then
 * CR_PG_STRT to commit them.
 *
 * ### The fence the SDK leaves out
 *
 * Each word written into the page buffer must be complete before the WR_BSY
 * poll that follows it, and on this core a plain store followed by a load of
 * a different address is not enough to guarantee that. WCH's own
 * FLASH_ProgramPage_Fast() has `__asm("fence")` in that loop -- but wrapped in
 * `#ifdef Core_V5F`, a macro this build never defines, so the SDK copy
 * compiles WITHOUT it.
 *
 * That is why the sequence is written out here rather than called: the
 * SDK's version is correct only for a build that defines a macro we do not,
 * and the failure it produces is silent data corruption rather than an error.
 */

#define CR_PAGE_PG   ((uint32_t)0x00010000)
#define CR_PG_STRT   ((uint32_t)0x00200000)
#define CR_LOCK_Set  ((uint32_t)0x00000080)
#define CR_FLOCK_Set ((uint32_t)0x00008000)
#define SR_BSY       ((uint32_t)0x00000001)
#define SR_WR_BSY    ((uint32_t)0x00000002)
#define FLASH_KEY1   ((uint32_t)0x45670123)
#define FLASH_KEY2   ((uint32_t)0xCDEF89AB)

#define CH32H4_FLASH_PROG_PAGE 256u

static void flash_unlock_fast(void) {
    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;
    FLASH->MODEKEYR = FLASH_KEY1;
    FLASH->MODEKEYR = FLASH_KEY2;
}

static void flash_lock_fast(void) {
    FLASH->CTLR |= CR_FLOCK_Set;
    FLASH->CTLR |= CR_LOCK_Set;
}

/* One 256-byte page. `addr` is page-aligned and `src` holds 64 words.
 *
 * ### This runs from ITCM, and with interrupts off
 *
 * Page programming is not like erasing. An erase is one command and the flash
 * controller stalls the bus until it finishes, so code running from flash
 * simply pauses and continues -- which is why ch32h4_flash_erase() works from
 * flash and was verified doing so. Page programming needs the CPU to stay
 * running: it writes 64 words into the page buffer, polling between each one.
 * Those instruction fetches come from the flash being programmed, and the
 * board hangs in the first poll. Measured -- the erase and blank-check before
 * it both returned, and the write never did.
 *
 * So the loop lives in ITCM, and it calls nothing: a call to a flash-resident
 * function would fetch from flash just as surely as an inline instruction
 * would. `src` is already in RAM for the same reason.
 *
 * Interrupts are masked for the same reason again. A handler entered during
 * the window vectors through the trap table into flash-resident code, and the
 * failure is identical -- and much rarer, so it would present as a filesystem
 * that corrupts occasionally under load rather than as anything reproducible.
 */
/* Every wait here is BOUNDED.
 *
 * An unbounded spin inside this window is not a hang like any other. The part
 * is left in fast-program mode with CR_PAGE_PG set and the flash unlocked, and
 * in that state the debug probe cannot reach the flash controller either --
 * wlink reports protocol error 0x55 and the board needs a physical unplug and
 * a power cycle to recover. One wrong guess about the sequence costs a trip to
 * the bench.
 *
 * The bound is a loop count rather than a timer: this runs with interrupts
 * masked, so millis() does not advance, and it must not depend on anything
 * outside ITCM. A page program takes microseconds; a million iterations is
 * several orders of magnitude of headroom and still returns.
 */
#define FLASH_SPIN_LIMIT 1000000u

__itcm_func static bool flash_program_page(uint32_t addr, const uint32_t *src) {
    uint32_t prev;
    __asm volatile("csrrci %0, mstatus, 8" : "=r"(prev));

    uint32_t guard;
    bool ok = true;

    FLASH->CTLR |= CR_PAGE_PG;
    for (guard = FLASH_SPIN_LIMIT; (FLASH->STATR & SR_BSY) && guard; guard--) {
    }
    if (!guard) {
        ok = false;
    }
    for (guard = FLASH_SPIN_LIMIT; ok && (FLASH->STATR & SR_WR_BSY) && guard; guard--) {
    }
    if (!guard) {
        ok = false;
    }

    for (uint32_t i = 0; ok && i < CH32H4_FLASH_PROG_PAGE / 4u; i++) {
        *(volatile uint32_t *)(uintptr_t)(addr + i * 4u) = src[i];
        /* The one the SDK compiles out. Without it the buffer writes and the
         * WR_BSY poll below can be seen out of order, and words go missing. */
        __asm volatile ("fence" ::: "memory");
        for (guard = FLASH_SPIN_LIMIT;
             (FLASH->STATR & SR_WR_BSY) && guard; guard--) {
        }
        if (!guard) {
            ok = false;
        }
    }

    if (ok) {
        FLASH->CTLR |= CR_PG_STRT;
        for (guard = FLASH_SPIN_LIMIT;
             (FLASH->STATR & SR_BSY) && guard; guard--) {
        }
        if (!guard) {
            ok = false;
        }
    }

    /* Cleared on EVERY path, including the failing ones. Leaving CR_PAGE_PG
     * set is what turns a failed write into a board that needs unplugging. */
    FLASH->CTLR &= ~CR_PAGE_PG;

    if (prev & 8u) {
        __asm volatile("csrsi mstatus, 8");
    }
    return ok;
}

uint32_t ch32h4_flash_prog_size(void) {
    return CH32H4_FLASH_PROG_PAGE;
}

bool ch32h4_flash_write(uint32_t addr, const void *src, uint32_t len) {
    /* Whole pages only. The hardware has no partial-page commit, and rounding
     * outward here would program bytes the caller never supplied over data it
     * did not mention. LittleFS is configured with prog_size = 256 so that it
     * only ever asks for what this can do. */
    if (len == 0u) {
        return true;
    }
    if ((addr % CH32H4_FLASH_PROG_PAGE) != 0u
        || (len % CH32H4_FLASH_PROG_PAGE) != 0u) {
        return false;
    }

    /* Copied through an aligned buffer: the page loop reads whole words, and
     * LittleFS hands out pointers into its own cache with no alignment
     * promise. */
    uint32_t page[CH32H4_FLASH_PROG_PAGE / 4u];
    const uint8_t *s = (const uint8_t *)src;

    /* The other core must not be fetching from flash while this runs -- a page
     * program does not complete if it is, and the board hangs. Parked ONCE
     * around the whole write rather than per page: the other core is stopped
     * either way, and per page would multiply the handshake by the page
     * count. */
    if (!ch32h4_park_other(PARK_TIMEOUT_MS)) {
        return false;
    }

    bool ok = true;
    flash_unlock_fast();
    for (uint32_t off = 0; ok && off < len; off += CH32H4_FLASH_PROG_PAGE) {
        memcpy(page, s + off, CH32H4_FLASH_PROG_PAGE);
        ok = flash_program_page(addr + off, page);
    }
    /* Relocked even when a page failed, for the same reason CR_PAGE_PG is
     * cleared: an unlocked flash controller is a debug probe that cannot
     * connect. */
    flash_lock_fast();
    ch32h4_unpark_other();
    if (!ok) {
        return false;
    }

    /* Verified, because a program that reports success and did not take is the
     * failure that turns into a corrupt filesystem several boots later. The
     * page path reports no status of its own, so this IS the status. */
    return memcmp((const void *)(uintptr_t)addr, src, len) == 0;
}
