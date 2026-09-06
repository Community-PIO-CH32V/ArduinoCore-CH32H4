/* ChaN's disk layer, dispatched by volume. See ch32h4_fatfs_disk.h. */
#include "ch32h4_fatfs_disk.h"

static const ch32h4_fatfs_disk_ops *s_disk[FF_VOLUMES];

void ch32h4_fatfs_register_disk(BYTE pdrv, const ch32h4_fatfs_disk_ops *ops) {
    if (pdrv < FF_VOLUMES) {
        s_disk[pdrv] = ops;
    }
}

/* An unregistered volume is "not initialised", never an error: a sketch that
 * mounts SD without ever calling FatFS.begin() must get a clean answer for
 * volume 0 rather than a failure that looks like broken hardware. */
DSTATUS disk_status(BYTE pdrv) {
    if (pdrv >= FF_VOLUMES || !s_disk[pdrv]) {
        return STA_NOINIT;
    }
    return s_disk[pdrv]->status();
}

DSTATUS disk_initialize(BYTE pdrv) {
    if (pdrv >= FF_VOLUMES || !s_disk[pdrv]) {
        return STA_NOINIT;
    }
    return s_disk[pdrv]->initialize();
}

DRESULT disk_read(BYTE pdrv, BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv >= FF_VOLUMES || !s_disk[pdrv]) {
        return RES_NOTRDY;
    }
    return s_disk[pdrv]->read(buff, sector, count);
}

#if FF_FS_READONLY == 0

DRESULT disk_write(BYTE pdrv, const BYTE *buff, LBA_t sector, UINT count) {
    if (pdrv >= FF_VOLUMES || !s_disk[pdrv]) {
        return RES_NOTRDY;
    }
    return s_disk[pdrv]->write(buff, sector, count);
}

#endif

DRESULT disk_ioctl(BYTE pdrv, BYTE cmd, void *buff) {
    if (pdrv >= FF_VOLUMES || !s_disk[pdrv]) {
        return RES_NOTRDY;
    }
    return s_disk[pdrv]->ioctl(cmd, buff);
}
