/* Which driver serves which FatFs volume.
 *
 * ChaN's disk_read() and friends take a pdrv and have to reach two different
 * drivers -- the flash translation layer and the SD block driver. A registry
 * rather than a switch, for one reason: a switch in diskio.c would name both
 * drivers unconditionally, so every SD-only sketch would link the flash
 * translation layer and every flash-only sketch would link the SDMMC driver.
 * Through function pointers, only a driver that something actually registers
 * gets linked, and --gc-sections drops the rest.
 *
 * Registration happens from each filesystem's begin(), NOT from a static
 * constructor: a static constructor runs for every linked translation unit,
 * which would defeat exactly the collection this exists for.
 */
#pragma once

#include "ff.h"
#include "diskio.h"

#ifdef __cplusplus
extern "C" {
#endif

#define CH32H4_FATFS_PDRV_FLASH  0
#define CH32H4_FATFS_PDRV_SD     1

typedef struct {
    DSTATUS (*status)(void);
    DSTATUS (*initialize)(void);
    DRESULT (*read)(BYTE *buff, LBA_t sector, UINT count);
    DRESULT (*write)(const BYTE *buff, LBA_t sector, UINT count);
    DRESULT (*ioctl)(BYTE cmd, void *buff);
} ch32h4_fatfs_disk_ops;

/* `ops` must outlive the mount; pass a pointer to a static. Registering NULL
 * unregisters, which is what a filesystem's end() does. */
void ch32h4_fatfs_register_disk(BYTE pdrv, const ch32h4_fatfs_disk_ops *ops);

#ifdef __cplusplus
}
#endif
