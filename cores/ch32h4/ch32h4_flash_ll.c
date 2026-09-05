#include "ch32h4_flash_ll.h"

#include "ch32h4_itcm.h"
#include "ch32h417.h"

/* Register bits. The two that matter and are easy to confuse:
 *
 *   CR_STRT   starts an ERASE, and takes the address from FLASH->ADDR.
 *   CR_PG_STRT commits a PAGE PROGRAM, and takes no address at all -- the page
 *              buffer already knows where it goes.
 *
 * Using the first where the second belongs programs page zero and then fails
 * on every page after it. */
#define CR_PER_Set   ((uint32_t)0x00000002)
#define CR_STRT_Set  ((uint32_t)0x00000040)
#define CR_LOCK_Set  ((uint32_t)0x00000080)
#define CR_FLOCK_Set ((uint32_t)0x00008000)
#define CR_PAGE_PG   ((uint32_t)0x00010000)
#define CR_PG_STRT   ((uint32_t)0x00200000)
#define SR_BSY       ((uint32_t)0x00000001)
#define SR_WR_BSY    ((uint32_t)0x00000002)
#define FLASH_KEY1   ((uint32_t)0x45670123)
#define FLASH_KEY2   ((uint32_t)0xCDEF89AB)

/* The capacity bit the SDK consults to pick the erase page size. */
#define FLASH_CFGR0  (*(volatile uint32_t *)0x40022020u)

/* EVERY WAIT IS BOUNDED.
 *
 * An unbounded spin inside this window is not a hang like any other: the part
 * is left in fast-program mode with CR_PAGE_PG set and the flash unlocked, and
 * in that state the debug probe cannot reach the flash controller either --
 * wlink reports protocol error 0x55, and the board needs a physical unplug and
 * a power cycle. One wrong guess about the sequence costs a trip to the bench,
 * which is not hypothetical.
 *
 * A loop count rather than a timer, because this runs with interrupts masked
 * so millis() does not advance, and because nothing outside ITCM may be
 * called. A page program takes microseconds; a million iterations is several
 * orders of magnitude of headroom and still returns. */
#define SPIN_LIMIT 1000000u

__itcm_func static bool wait_bsy(void) {
    uint32_t guard = SPIN_LIMIT;
    while ((FLASH->STATR & SR_BSY) && guard) {
        guard--;
    }
    return guard != 0u;
}

__itcm_func static bool wait_wr(void) {
    uint32_t guard = SPIN_LIMIT;
    while ((FLASH->STATR & SR_WR_BSY) && guard) {
        guard--;
    }
    return guard != 0u;
}

__itcm_func void ch32h4_flash_ll_unlock(void) {
    FLASH->KEYR = FLASH_KEY1;
    FLASH->KEYR = FLASH_KEY2;
    FLASH->MODEKEYR = FLASH_KEY1;
    FLASH->MODEKEYR = FLASH_KEY2;
}

__itcm_func void ch32h4_flash_ll_lock(void) {
    FLASH->CTLR |= CR_FLOCK_Set;
    FLASH->CTLR |= CR_LOCK_Set;
}

__itcm_func uint32_t ch32h4_flash_ll_page_size(void) {
    return (FLASH_CFGR0 & (1u << 28)) ? 8192u : 4096u;
}

__itcm_func bool ch32h4_flash_ll_erase_page(uint32_t addr) {
    uint32_t prev;
    __asm volatile("csrrci %0, mstatus, 8" : "=r"(prev));

    bool ok = wait_bsy();
    if (ok) {
        FLASH->CTLR |= CR_PER_Set;
        FLASH->ADDR = addr;
        FLASH->CTLR |= CR_STRT_Set;
        ok = wait_bsy();
        /* Cleared on every path: leaving CR_PER set is what turns a failed
         * erase into a board that needs unplugging. */
        FLASH->CTLR &= ~CR_PER_Set;
    }

    if (prev & 8u) {
        __asm volatile("csrsi mstatus, 8");
    }
    return ok;
}

__itcm_func bool ch32h4_flash_ll_program_page(uint32_t addr,
                                              const uint32_t *src) {
    uint32_t prev;
    __asm volatile("csrrci %0, mstatus, 8" : "=r"(prev));

    bool ok = true;
    FLASH->CTLR |= CR_PAGE_PG;
    ok = wait_bsy() && wait_wr();

    for (uint32_t i = 0; ok && i < CH32H4_FLASH_LL_PROG_PAGE / 4u; i++) {
        *(volatile uint32_t *)(uintptr_t)(addr + i * 4u) = src[i];
        /* The fence the SDK compiles out. Without it the buffer writes and the
         * WR_BSY poll below can be seen out of order, and words go missing. */
        __asm volatile("fence" ::: "memory");
        ok = wait_wr();
    }

    if (ok) {
        /* CR_PG_STRT, not CR_STRT_Set. See the note on the defines above. */
        FLASH->CTLR |= CR_PG_STRT;
        ok = wait_bsy();
    }

    /* Cleared on EVERY path, including the failing ones. */
    FLASH->CTLR &= ~CR_PAGE_PG;

    if (prev & 8u) {
        __asm volatile("csrsi mstatus, 8");
    }
    return ok;
}
