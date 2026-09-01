/* FatFs' disk layer, onto the SDMMC block driver.
 *
 * There is one drive, so pdrv is checked and otherwise ignored. Everything
 * interesting is a block below this: see cores/ch32h4/ch32h4_sdmmc.c.
 */
#include "ff.h"
#include "diskio.h"

#include "ch32h4_sdmmc.h"

DSTATUS disk_status(BYTE pdrv) {
    if (pdrv != 0) {
        return STA_NOINIT;
    }
    return ch32h4_sd_ready() ? 0 : STA_NOINIT;
}

DSTATUS disk_initialize(BYTE pdrv) {
    /* Deliberately does NOT bring the card up.
     *
     * FatFs calls this on the first access to a volume, and a sketch that has
     * not called SDFS.begin() should get "no card" rather than have a card
     * silently initialised at whatever default this file happened to pick.
     * The bus width and clock are the sketch's to choose. */
    return disk_status(pdrv);
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0) {
        return RES_PARERR;
    }
    if (!ch32h4_sd_ready()) {
        return RES_NOTRDY;
    }
    return ch32h4_sd_read_blocks((uint32_t)sector, buff, count) == 0
               ? RES_OK : RES_ERROR;
}

#if FF_FS_READONLY == 0

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv != 0) {
        return RES_PARERR;
    }
    if (!ch32h4_sd_ready()) {
        return RES_NOTRDY;
    }
    return ch32h4_sd_write_blocks((uint32_t)sector, buff, count) == 0
               ? RES_OK : RES_ERROR;
}

#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv != 0) {
        return RES_PARERR;
    }
    if (!ch32h4_sd_ready()) {
        return RES_NOTRDY;
    }

    switch (cmd) {
        case CTRL_SYNC:
            /* The block driver waits for the card to leave the programming
             * state at the end of every write, so nothing is outstanding by
             * the time control gets back here. */
            return RES_OK;

        case GET_SECTOR_COUNT:
            *(LBA_t *)buff = ch32h4_sd.block_count;
            return RES_OK;

        case GET_SECTOR_SIZE:
            *(WORD *)buff = CH32H4_SD_BLOCK_SIZE;
            return RES_OK;

        case GET_BLOCK_SIZE:
            /* The erase-block size, in sectors, which f_mkfs uses to align
             * the data area. It is in the CSD, but reading it wrong produces
             * a filesystem that works and wears the card unevenly, so 1
             * ("unknown") is the honest answer until it is actually decoded. */
            *(DWORD *)buff = 1;
            return RES_OK;

        default:
            return RES_PARERR;
    }
}

/* FatFs stamps this into directory entries. There is no RTC configured here
 * yet, so it returns the fixed date FF_NORTC_* names -- FatFs would do the
 * same if FF_FS_NORTC were 1, but supplying it means an RTC can be wired in
 * later by changing this one function. */
DWORD get_fattime(void) {
    return ((DWORD)(FF_NORTC_YEAR - 1980) << 25)
           | ((DWORD)FF_NORTC_MON << 21)
           | ((DWORD)FF_NORTC_MDAY << 16);
}
