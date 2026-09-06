/* FatFs' disk layer for the SD card, on the SDMMC block driver.
 *
 * This is volume 1; volume 0 is the internal flash, which libraries/FatFS
 * mounts. Everything interesting is a block below this: see
 * cores/ch32h4/ch32h4_sdmmc.c.
 *
 * Registered from SDFSImpl::begin() rather than from a static constructor --
 * see ch32h4_fatfs_disk.h for why that matters.
 */
#include "ch32h4_fatfs_disk.h"

#include "ch32h4_sdmmc.h"

static DSTATUS sd_status(void) {
    return ch32h4_sd_ready() ? 0 : STA_NOINIT;
}

static DSTATUS sd_initialize(void) {
    /* Deliberately does NOT bring the card up.
     *
     * FatFs calls this on the first access to a volume, and a sketch that has
     * not called SDFS.begin() should get "no card" rather than have a card
     * silently initialised at whatever default this file happened to pick.
     * The bus width and clock are the sketch's to choose. */
    return sd_status();
}

static DRESULT sd_read(BYTE *buff, LBA_t sector, UINT count) {
    if (!ch32h4_sd_ready()) {
        return RES_NOTRDY;
    }
    return ch32h4_sd_read_blocks((uint32_t)sector, buff, count) == 0
               ? RES_OK : RES_ERROR;
}

#if FF_FS_READONLY == 0

static DRESULT sd_write(const BYTE *buff, LBA_t sector, UINT count) {
    if (!ch32h4_sd_ready()) {
        return RES_NOTRDY;
    }
    return ch32h4_sd_write_blocks((uint32_t)sector, buff, count) == 0
               ? RES_OK : RES_ERROR;
}

#endif

static DRESULT sd_ioctl(BYTE cmd, void *buff) {
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

static const ch32h4_fatfs_disk_ops s_sd_ops = {
    sd_status,
    sd_initialize,
    sd_read,
#if FF_FS_READONLY == 0
    sd_write,
#else
    NULL,
#endif
    sd_ioctl
};

void ch32h4_sd_disk_register(void) {
    ch32h4_fatfs_register_disk(CH32H4_FATFS_PDRV_SD, &s_sd_ops);
}
