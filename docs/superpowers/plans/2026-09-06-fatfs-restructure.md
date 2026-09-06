# FatFS Restructure Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** One copy of ChaN's FatFs, built for two volumes, serving a FAT filesystem on internal flash (`FatFS`), a FAT filesystem on SD (`SDFS`), and a USB mass-storage view of the flash volume (`FatFSUSB`) — all mountable at the same time.

**Architecture:** FatFs moves out of `SDFS/src/fatfs/` into a new `FatFS` library that owns the only copy. `diskio.c` dispatches on `pdrv` through a registry that each filesystem fills in from its `begin()`, so an SD-only sketch never links the flash translation layer and a flash-only sketch never links the SDMMC driver. Volume 0 is flash, reached through a ported wear-levelling FTL that presents 512-byte LBAs over 8 KB erase pages; volume 1 is SD, unchanged underneath.

**Tech Stack:** ChaN FatFs (already vendored), SPIFTL from arduino-pico, `cores/ch32h4/ch32h4_flash.{h,c}`, `cores/ch32h4/FS.h`/`FSImpl.h`, `Adafruit_USBD_MSC`, pytest hardware harness in `tests/hw`.

**Spec:** `docs/superpowers/specs/2026-09-06-fatfs-restructure-design.md`

## Global Constraints

- `FF_VOLUMES 2`. Volume 0 is flash (`CH32H4_FATFS_PDRV_FLASH`), volume 1 is SD (`CH32H4_FATFS_PDRV_SD`).
- `FF_MIN_SS 512` and `FF_MAX_SS 512`. The FTL presents 512-byte LBAs so both volumes agree.
- `FF_STR_VOLUME_ID 0`. Prefixes are `"0:"` and `"1:"`, internal to each `FSImpl`; sketch-visible paths stay unprefixed.
- `FF_FS_REENTRANT 0`. FatFs is called from one core only.
- **256 KB is a hard minimum for the flash volume**, enforced in `FatFSImpl::begin()`, which fails naming the board menu setting.
- `FatFSConfig` defaults `autoFormat = true`, matching `LittleFSConfig` and `FSConfig` in this core.
- `SD` and `SDFS` stay source-compatible. Existing sketches compile and run unchanged.
- `ch32h4_flash_write(addr, src, len)` requires **`addr` and `len` both multiples of 256**. It returns false otherwise; it does not round.
- Every library's public headers live at the top of `src/`, because arduino-cli resolves libraries by those headers and compiles `src/` recursively.
- Commit messages end with `Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>`.
- Hardware tests use the pinned wlink at `tools/bin/wlink.exe` (0.1.2). PlatformIO's `tool-wlink` is 0.1.1 and reports "Probe is not attached to an MCU" on this part regardless of health.

---

## File Structure

**Created:**

| File | Responsibility |
|---|---|
| `libraries/FatFS/library.properties` | library metadata, no `depends` |
| `libraries/FatFS/src/ff.c`, `ff.h`, `ffunicode.c`, `ffconf.h` | ChaN FatFs, moved from SDFS, `FF_VOLUMES 2` |
| `libraries/FatFS/src/diskio.h` | ChaN's, moved unchanged |
| `libraries/FatFS/src/diskio.c` | dispatch `pdrv` -> registry |
| `libraries/FatFS/src/ch32h4_fatfs_disk.h` | registry interface, `ch32h4_fatfs_disk_ops` |
| `libraries/FatFS/src/ch32h4_fatfs_time.c` | `get_fattime()`, moved from SDFS's diskio |
| `libraries/FatFS/src/SPIFTL.h`, `FlashInterface.h` | ported FTL, `ebBytes` parameterised |
| `libraries/FatFS/src/ch32h4_ftl_flash.h`, `.cpp` | `FlashInterface` over `ch32h4_flash_*` |
| `libraries/FatFS/src/FatFS.h`, `.cpp` | `FatFSImpl : FSImpl`, volume 0 |
| `libraries/SDFS/src/ch32h4_sd_disk.c` | SD driver, registers as pdrv 1 |
| `libraries/FatFSUSB/**` | MSC over volume 0 |
| `tests/sketches/fatfstest/`, `tests/hw/test_fatfs.py` | flash volume |
| `tests/sketches/twovoltest/`, `tests/hw/test_two_volumes.py` | both volumes at once |

**Modified:** `libraries/SDFS/src/SDFS.cpp` (volume prefix), `SDFS/library.properties` (`depends=FatFS`), `tests/hw/conftest.py` (two fixtures), `docs/hazards.md` (USB stall measurement).

**Deleted:** `libraries/SDFS/src/fatfs/` entirely.

---

## Task 1: One FatFs, two volumes, SDFS on volume 1

A pure refactor. No new behaviour, and the existing SD hardware tests are the proof: if `test_filesystem.py` still passes, the move was clean. Land this on its own.

**Files:**
- Create: `libraries/FatFS/library.properties`, `libraries/FatFS/src/ch32h4_fatfs_disk.h`, `libraries/FatFS/src/diskio.c`, `libraries/FatFS/src/ch32h4_fatfs_time.c`
- Move: `libraries/SDFS/src/fatfs/{ff.c,ff.h,ffconf.h,ffunicode.c,diskio.h}` → `libraries/FatFS/src/`
- Create: `libraries/SDFS/src/ch32h4_sd_disk.c` (from `SDFS/src/fatfs/ch32h4_diskio.c`)
- Delete: `libraries/SDFS/src/fatfs/`
- Modify: `libraries/SDFS/src/SDFS.cpp`, `libraries/SDFS/src/SDFS.h`, `libraries/SDFS/library.properties`
- Test: `tests/hw/test_filesystem.py` (existing, unchanged — it is the regression gate)

**Interfaces:**
- Produces: `ch32h4_fatfs_disk_ops` struct and `void ch32h4_fatfs_register_disk(BYTE pdrv, const ch32h4_fatfs_disk_ops *ops)`; the constants `CH32H4_FATFS_PDRV_FLASH` (0) and `CH32H4_FATFS_PDRV_SD` (1). Tasks 2–4 consume these.

- [ ] **Step 1: Move the FatFs sources**

```bash
cd C:/Users/Max/temp/arduino-core-ch32h4
mkdir -p libraries/FatFS/src
git mv libraries/SDFS/src/fatfs/ff.c        libraries/FatFS/src/ff.c
git mv libraries/SDFS/src/fatfs/ff.h        libraries/FatFS/src/ff.h
git mv libraries/SDFS/src/fatfs/ffconf.h    libraries/FatFS/src/ffconf.h
git mv libraries/SDFS/src/fatfs/ffunicode.c libraries/FatFS/src/ffunicode.c
git mv libraries/SDFS/src/fatfs/diskio.h    libraries/FatFS/src/diskio.h
git mv libraries/SDFS/src/fatfs/ch32h4_diskio.c libraries/SDFS/src/ch32h4_sd_disk.c
```

- [ ] **Step 2: Write the registry header**

Create `libraries/FatFS/src/ch32h4_fatfs_disk.h`:

```c
/* Which driver serves which FatFs volume.
 *
 * ChaN's disk_read() and friends take a pdrv and have to reach two different
 * drivers -- the flash translation layer and the SD block driver. A registry
 * rather than a switch, for one reason: a switch in diskio.c would name both
 * drivers unconditionally, so every SD-only sketch would link the flash FTL
 * and every flash-only sketch would link the SDMMC driver. Through function
 * pointers, only a driver that something actually registers gets linked, and
 * --gc-sections drops the rest.
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
```

- [ ] **Step 3: Write the dispatcher**

Create `libraries/FatFS/src/diskio.c`:

```c
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
```

- [ ] **Step 4: Split `get_fattime()` out of the SD driver**

`get_fattime()` is FatFs-wide, not SD's. Cut it (and its `#include <time.h>`, `"ch32h4_rtc.h"`, and its whole explanatory comment) out of `libraries/SDFS/src/ch32h4_sd_disk.c` and paste it verbatim into a new `libraries/FatFS/src/ch32h4_fatfs_time.c`, with this header above it:

```c
/* The timestamp FatFs stamps into every directory entry it writes.
 *
 * Here rather than in a disk driver because it belongs to no volume: files on
 * flash and files on SD get their time from the same clock.
 */
#include "ff.h"

#include <time.h>

#include "ch32h4_rtc.h"
```

- [ ] **Step 5: Convert the SD driver to the registry**

Rewrite `libraries/SDFS/src/ch32h4_sd_disk.c` so each function loses its `pdrv` parameter and the `pdrv != 0` guard, and add the ops table plus a registration entry point at the end. The bodies are otherwise unchanged:

```c
/* FatFs' disk layer for the SD card, on the SDMMC block driver.
 *
 * This is volume 1. Everything interesting is a block below it: see
 * cores/ch32h4/ch32h4_sdmmc.c.
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

static DRESULT sd_write(const BYTE *buff, LBA_t sector, UINT count) {
    if (!ch32h4_sd_ready()) {
        return RES_NOTRDY;
    }
    return ch32h4_sd_write_blocks((uint32_t)sector, buff, count) == 0
               ? RES_OK : RES_ERROR;
}

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
    sd_status, sd_initialize, sd_read, sd_write, sd_ioctl
};

void ch32h4_sd_disk_register(void) {
    ch32h4_fatfs_register_disk(CH32H4_FATFS_PDRV_SD, &s_sd_ops);
}
```

Declare it in `libraries/SDFS/src/SDFS.h`, inside the existing `extern "C"` block that already declares the SDMMC functions:

```c
void ch32h4_sd_disk_register(void);
```

- [ ] **Step 6: Point SDFS at volume 1**

In `libraries/SDFS/src/SDFS.cpp`, replace the `fixPath()` helper with one that prefixes the volume. Every FatFs call in the file then goes through it.

```cpp
/* FatFs speaks absolute paths without a leading slash just as happily as with
 * one, but "" is not a path at all and "/" is. Normalising here means the rest
 * of this file never has to think about it.
 *
 * It also carries the volume prefix. The SD card is FatFs volume 1 -- volume 0
 * is the internal flash, which FatFS.h mounts -- and this is the single point
 * where a sketch-visible path becomes a FatFs one. Sketches never see "1:".
 *
 * Returns a String, not a const char *, because the prefix has to be stored
 * somewhere; callers pass .c_str() and the temporary outlives the call. */
static String fixPath(const char *path) {
    if (!path || !path[0]) {
        return String(SD_VOL "/");
    }
    if (path[0] == '/') {
        return String(SD_VOL) + path;
    }
    return String(SD_VOL "/") + path;
}
```

with, near the top of the file:

```cpp
/* FatFs volume 1. See ch32h4_fatfs_disk.h for the numbering. */
#define SD_VOL "1:"
```

Then update every call site in `SDFS.cpp` — the compiler finds them all once `fixPath` returns `String`:

| line (before) | change |
|---|---|
| `f_mount(&_fs, "", 1)` (×2 in `begin()`) | `f_mount(&_fs, SD_VOL, 1)` |
| `f_mount(nullptr, "", 0)` in `end()` | `f_mount(nullptr, SD_VOL, 0)` |
| `f_mkfs("", &opt, work, FF_MAX_SS)` | `f_mkfs(SD_VOL, &opt, work, FF_MAX_SS)` |
| `f_getfree("", &freeClusters, &fs)` | `f_getfree(SD_VOL, &freeClusters, &fs)` |
| `f_open(&fil, fixPath(path), mode)` | `f_open(&fil, fixPath(path).c_str(), mode)` |
| `f_stat(fixPath(path), &fno)` | `f_stat(fixPath(path).c_str(), &fno)` |
| `f_rename(fixPath(from), fixPath(to))` | `f_rename(fixPath(from).c_str(), fixPath(to).c_str())` |
| `f_unlink(fixPath(path))` (×2) | `f_unlink(fixPath(path).c_str())` |
| `f_mkdir(fixPath(path))` | `f_mkdir(fixPath(path).c_str())` |
| `std::make_shared<SDFSFileImpl>(this, fixPath(path), ...)` | `fixPath(path).c_str()` |

`SDFSDirImpl` stores `_path` as a `String` already and calls `f_opendir(&_dir, _path.c_str())`; make sure the `_path` it is constructed with came from `fixPath()`. `SDFSFileImpl::_name` likewise already holds the prefixed path, so its `f_stat(_name, &fno)` needs no change.

- [ ] **Step 7: Register the SD driver from `begin()`**

At the top of `SDFSImpl::begin()` in `SDFS.cpp`, before the `f_mount`:

```cpp
    /* Not a static constructor: see ch32h4_fatfs_disk.h. Idempotent, so
       calling begin() twice is harmless. */
    ch32h4_sd_disk_register();
```

and in `SDFSImpl::end()`, after the `f_mount(nullptr, ...)`:

```cpp
    ch32h4_fatfs_register_disk(CH32H4_FATFS_PDRV_SD, nullptr);
```

- [ ] **Step 8: Set the volume count**

In `libraries/FatFS/src/ffconf.h`:

```c
#define FF_VOLUMES      2
```

Leave `FF_STR_VOLUME_ID`, `FF_MIN_SS`, `FF_MAX_SS`, `FF_MULTI_PARTITION` and `FF_FS_REENTRANT` exactly as they are. Add above `FF_VOLUMES`:

```c
/* Two: volume 0 is the internal flash (libraries/FatFS), volume 1 is the SD
 * card (libraries/SDFS). Both are 512-byte-sector devices -- the flash gets
 * there through a translation layer -- so FF_MIN_SS and FF_MAX_SS stay equal
 * and FatFs keeps its fixed-sector fast paths. */
```

- [ ] **Step 9: Library metadata**

Create `libraries/FatFS/library.properties`:

```
name=FatFS
version=1.0.0
author=Community-PIO-CH32V
maintainer=Community-PIO-CH32V
sentence=FAT filesystem on the internal flash, and the FatFs shared by SDFS.
paragraph=ChaN's FatFs, built for two volumes: the internal flash partition through a wear-levelling translation layer, and the SD card through SDFS. Mount either or both. FatFSUSB exposes the flash volume to a PC as a USB stick.
category=Data Storage
architectures=ch32h4
```

Append to `libraries/SDFS/library.properties`:

```
depends=FatFS
```

- [ ] **Step 10: Build the existing SD example to prove the move**

```bash
cd C:/Users/Max/temp/arduino-core-ch32h4
python tools/buildexamples.py SD SDFS
python tools/buildexamples.py --ide SD SDFS
```

Expected: all examples build under both. A failure naming `fatfs/ff.h` means an include path was missed — `SDFS.h` includes `"fatfs/ff.h"` today and must become `<ff.h>`.

- [ ] **Step 11: Run the SD hardware tests — the regression gate**

```bash
cd C:/Users/Max/temp/arduino-core-ch32h4
WLINK=tools/bin/wlink.exe python -m pytest tests/hw/test_filesystem.py -q
```

Expected: the same pass/skip counts as before the change. These tests do not know the volume moved, which is exactly why they are the right gate.

- [ ] **Step 12: Commit**

```bash
git add -A
git commit -m "One FatFs, two volumes, with SDFS on volume 1

FatFs moves out of SDFS into a FatFS library that owns the only copy, built
for two volumes so a flash filesystem can sit alongside the SD card.

diskio.c dispatches on pdrv through a registry each filesystem fills in from
its begin(), rather than a switch. The switch would have named both drivers
unconditionally, so every SD-only sketch would link the flash translation
layer that Task 2 adds and every flash-only sketch would link SDMMC.

No behaviour changes. SDFS gains a \"1:\" prefix at the one place a
sketch-visible path becomes a FatFs one, and the existing SD hardware tests
pass unchanged -- which is the point of landing this on its own.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 2: The flash translation layer

Port SPIFTL and prove it against real flash **before** any filesystem sits on it. A bug here surfaces as unexplainable FAT corruption two tasks later.

**Files:**
- Create: `libraries/FatFS/src/FlashInterface.h`, `SPIFTL.h` (ported), `ch32h4_ftl_flash.h`, `ch32h4_ftl_flash.cpp`
- Create: `tests/sketches/fatfstest/platformio.ini`, `tests/sketches/fatfstest/src/main.cpp`
- Create: `tests/hw/test_ftl.py`
- Modify: `tests/hw/conftest.py`

**Interfaces:**
- Consumes: `ch32h4_flash_erase/write/read/page_size/prog_size/size` from `cores/ch32h4/ch32h4_flash.h`.
- Produces: `class CH32H4FTLFlash : public FlashInterface` with constructor `CH32H4FTLFlash(uint32_t base, uint32_t len)`; `SPIFTL(FlashInterface *fi, int ebBytes)`; `SPIFTL::lbaCount()`, `::read(int lba, void *dst)`, `::write(int lba, const void *src)`, `::format()`, `::start()`, `::persist()`.

- [ ] **Step 1: Copy the FTL, keeping attribution**

```bash
cd C:/Users/Max/temp/arduino-core-ch32h4
cp ../arduino-pico/libraries/FatFS/lib/SPIFTL/SPIFTL.h         libraries/FatFS/src/SPIFTL.h
cp ../arduino-pico/libraries/FatFS/lib/SPIFTL/FlashInterface.h libraries/FatFS/src/FlashInterface.h
```

Do not touch the existing copyright headers. Add below each, before the code:

```
    Ported to the CH32H41x by Community-PIO-CH32V, 2026. The only change of
    substance is that the erase-block size is a constructor parameter rather
    than a constant: this part has 8192-byte erase pages, and 4096 on the
    480 KB variant.
```

- [ ] **Step 2: Make the erase-block size a parameter**

In `libraries/FatFS/src/SPIFTL.h`, change the constructor and the two constants:

```cpp
    SPIFTL(FlashInterface *fi, int ebBytesIn) : ebBytes(ebBytesIn), _fi(fi) {
```

and at line ~273 replace

```cpp
    const int ebBytes = 4096;
    const int lbaBytes = 512;
```

with

```cpp
    /* 8192 on the 960 KB part, 4096 on the 480 KB one. A constant upstream,
       because the RP2040's flash erase is always 4096. */
    const int ebBytes;
    const int lbaBytes = 512;
```

Note `ebBytes` must be declared **before** any member whose initialiser uses it, and initialised first in the constructor's member-initialiser list, or `eraseBlocks = flashBytes / ebBytes` in the constructor body divides by garbage. Since the body does the arithmetic, declaration order only has to satisfy `-Wreorder`.

Then replace every remaining hardcoded geometry constant. There are eight `4096`s and one `512` outside the two declarations:

| line (approx) | before | after |
|---|---|---|
| 454 | `metadataCRC.add(eb, 4096 - 4);` | `metadataCRC.add(eb, ebBytes - 4);` |
| 456 | `memcmp(&crc, eb + 4096 - 4, 4)` | `memcmp(&crc, eb + ebBytes - 4, 4)` |
| 496 | `metadataEBoffset == 4096 - 4` | `metadataEBoffset == ebBytes - 4` |
| 499 | `..., 4096 - flashWriteBufferSize, ...` | `..., ebBytes - flashWriteBufferSize, ...` |
| 597 | `metadataCRC.add(eb, 4096 - 4);` | `metadataCRC.add(eb, ebBytes - 4);` |
| 599 | `memcmp(&crc, eb + 4096 - 4, 4)` | `memcmp(&crc, eb + ebBytes - 4, 4)` |
| 637 | `metadataEBoffset >= 4096 - 4` | `metadataEBoffset >= ebBytes - 4` |
| 917 | `readAddr + 512 * l2p_idx(i) + j` | `readAddr + lbaBytes * l2p_idx(i) + j` |
| 918 | `_fi->program(destEB, 512 * curIdx + j, ...)` | `_fi->program(destEB, lbaBytes * curIdx + j, ...)` |

Verify none remain:

```bash
grep -nE "\b4096\b" libraries/FatFS/src/SPIFTL.h
```

Expected: no output.

- [ ] **Step 3: Write the flash interface**

Create `libraries/FatFS/src/ch32h4_ftl_flash.h`:

```cpp
/* SPIFTL's view of our internal flash.
 *
 * The FTL thinks in erase blocks and byte offsets within them; this turns
 * those into the absolute addresses ch32h4_flash_* wants, bounded to the
 * filesystem partition so a bug here cannot reach the sketch.
 *
 * readEB() hands back a pointer rather than copying, because this flash is
 * memory-mapped at 0x08000000 and the FTL reads far more than it writes.
 */
#pragma once

#include <Arduino.h>
#include "FlashInterface.h"

class CH32H4FTLFlash : public FlashInterface {
public:
    /* `base` is an absolute 0x08... address and must be erase-page aligned;
       `len` a whole number of erase pages. */
    CH32H4FTLFlash(uint32_t base, uint32_t len);

    int size() override { return (int)_len; }
    int writeBufferSize() override { return (int)_progSize; }

    const uint8_t *readEB(int eb) override;
    bool eraseBlock(int eb) override;
    bool program(int eb, int offset, const void *data, int size) override;
    bool read(int eb, int offset, void *data, int size) override;

    uint32_t ebBytes() const { return _ebBytes; }

private:
    bool inRange(int eb, int offset, int size) const;

    uint32_t _base;
    uint32_t _len;
    uint32_t _ebBytes;
    uint32_t _progSize;
};
```

- [ ] **Step 4: Implement it**

Create `libraries/FatFS/src/ch32h4_ftl_flash.cpp`:

```cpp
#include "ch32h4_ftl_flash.h"

extern "C" {
#include "ch32h4_flash.h"
}

CH32H4FTLFlash::CH32H4FTLFlash(uint32_t base, uint32_t len)
    : _base(base), _len(len),
      _ebBytes(ch32h4_flash_page_size()),
      _progSize(ch32h4_flash_prog_size()) {
}

bool CH32H4FTLFlash::inRange(int eb, int offset, int size) const {
    if (eb < 0 || offset < 0 || size < 0) {
        return false;
    }
    const uint64_t start = (uint64_t)(uint32_t)eb * _ebBytes + (uint32_t)offset;
    return start + (uint32_t)size <= _len;
}

const uint8_t *CH32H4FTLFlash::readEB(int eb) {
    if (!inRange(eb, 0, (int)_ebBytes)) {
        return nullptr;
    }
    /* Memory-mapped, so no copy. */
    return (const uint8_t *)(uintptr_t)(_base + (uint32_t)eb * _ebBytes);
}

bool CH32H4FTLFlash::eraseBlock(int eb) {
    if (!inRange(eb, 0, (int)_ebBytes)) {
        return false;
    }
    return ch32h4_flash_erase(_base + (uint32_t)eb * _ebBytes, _ebBytes);
}

bool CH32H4FTLFlash::program(int eb, int offset, const void *data, int size) {
    if (!inRange(eb, offset, size)) {
        return false;
    }
    /* ch32h4_flash_write() takes whole 256-byte pages and refuses anything
     * else -- it will not round outward, because that would program bytes the
     * caller never supplied over data it did not mention. Every SPIFTL write
     * is writeBufferSize() or lbaBytes, both multiples of 256, so this should
     * never trip; it is checked rather than assumed because the failure is a
     * silently corrupt filesystem. */
    if ((offset % (int)_progSize) != 0 || (size % (int)_progSize) != 0) {
        return false;
    }
    return ch32h4_flash_write(_base + (uint32_t)eb * _ebBytes + (uint32_t)offset,
                              data, (uint32_t)size);
}

bool CH32H4FTLFlash::read(int eb, int offset, void *data, int size) {
    if (!inRange(eb, offset, size)) {
        return false;
    }
    ch32h4_flash_read(_base + (uint32_t)eb * _ebBytes + (uint32_t)offset,
                      data, (uint32_t)size);
    return true;
}
```

- [ ] **Step 5: Write the test sketch**

Create `tests/sketches/fatfstest/platformio.ini` by copying `tests/sketches/lfstest/platformio.ini` unchanged.

Create `tests/sketches/fatfstest/src/main.cpp`. This task only needs the FTL commands; Task 3 adds filesystem ones to the same sketch.

```cpp
/* The flash translation layer, and later the FAT filesystem on top of it.
 *
 * On Serial1 and without waiting for a host, like every other hardware test
 * sketch here: the thing under test must not need the thing testing it.
 */
#include <Arduino.h>
#include <ch32h4_ftl_flash.h>
#include <SPIFTL.h>

extern "C" {
#include "ch32h4_flash.h"
}

extern "C" char _FS_start[];
extern "C" char _FS_end[];

static CH32H4FTLFlash *flash = nullptr;
static SPIFTL *ftl = nullptr;

static void ftlInfo() {
  Serial1.print("fs_start=0x"); Serial1.println((uint32_t)(uintptr_t)_FS_start, HEX);
  Serial1.print("fs_size="); Serial1.println((uint32_t)(_FS_end - _FS_start));
  Serial1.print("eb_bytes="); Serial1.println(ch32h4_flash_page_size());
  Serial1.print("prog_size="); Serial1.println(ch32h4_flash_prog_size());
  if (ftl) {
    Serial1.print("ftl_lbas="); Serial1.println(ftl->lbaCount());
    Serial1.print("ftl_ebs="); Serial1.println(ftl->ebCount());
  }
}

static void ftlCreate() {
  if (!ftl) {
    flash = new CH32H4FTLFlash((uint32_t)(uintptr_t)_FS_start,
                               (uint32_t)(_FS_end - _FS_start));
    ftl = new SPIFTL(flash, (int)flash->ebBytes());
  }
  Serial1.println("ftl_create=1");
}

/* A pattern that depends on the LBA, so a read returning the wrong block is a
   failure rather than a coincidence. */
static void fillPattern(uint8_t *buf, int lba, uint8_t salt) {
  for (int i = 0; i < 512; i++) {
    buf[i] = (uint8_t)(lba * 7 + i * 3 + salt);
  }
}

static void ftlWrite(int lba, uint8_t salt) {
  uint8_t buf[512];
  fillPattern(buf, lba, salt);
  Serial1.print("ftl_write="); Serial1.println(ftl->write(lba, buf) ? 1 : 0);
}

static void ftlVerify(int lba, uint8_t salt) {
  uint8_t buf[512];
  uint8_t want[512];
  fillPattern(want, lba, salt);
  bool ok = ftl->read(lba, buf);
  Serial1.print("ftl_read="); Serial1.println(ok ? 1 : 0);
  Serial1.print("ftl_match=");
  Serial1.println(ok && !memcmp(buf, want, 512) ? 1 : 0);
}

void setup() {
  Serial1.begin(115200);
  Serial1.println();
  Serial1.println("fatfstest starting");
  Serial1.print("> ");
}

void loop() {
  static String line;
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        int sp = line.indexOf(' ');
        String cmd = sp < 0 ? line : line.substring(0, sp);
        String arg = sp < 0 ? String() : line.substring(sp + 1);

        if (cmd == "info") {
          ftlInfo();
        } else if (cmd == "ftlcreate") {
          ftlCreate();
        } else if (cmd == "ftlformat") {
          ftlCreate();
          Serial1.print("ftl_format="); Serial1.println(ftl->format() ? 1 : 0);
        } else if (cmd == "ftlstart") {
          ftlCreate();
          Serial1.print("ftl_start="); Serial1.println(ftl->start() ? 1 : 0);
        } else if (cmd == "ftlpersist") {
          Serial1.print("ftl_persist="); Serial1.println(ftl->persist() ? 1 : 0);
        } else if (cmd == "ftlwrite") {
          ftlWrite(arg.toInt(), 0);
        } else if (cmd == "ftlverify") {
          ftlVerify(arg.toInt(), 0);
        } else if (cmd == "ftlrewrite") {
          ftlWrite(arg.toInt(), 0x5A);
        } else if (cmd == "ftlreverify") {
          ftlVerify(arg.toInt(), 0x5A);
        } else if (cmd == "ftlchurn") {
          /* Force garbage collection: write one LBA far more times than there
             are erase blocks, so the FTL must reclaim. */
          int n = arg.toInt();
          bool ok = true;
          for (int i = 0; i < n && ok; i++) {
            uint8_t buf[512];
            fillPattern(buf, 3, (uint8_t)i);
            ok = ftl->write(3, buf);
          }
          Serial1.print("ftl_churn="); Serial1.println(ok ? 1 : 0);
        } else {
          Serial1.print("unknown: "); Serial1.println(cmd);
        }
        line = "";
      }
      Serial1.print("> ");
    } else {
      line += c;
    }
  }
}
```

- [ ] **Step 6: Add the board fixture**

In `tests/hw/conftest.py`, next to `lfs_board`:

```python
@pytest.fixture(scope="session")
def fatfs_board():
    """The flash translation layer and the FAT filesystem above it.

    Its own image for the same reason lfs_board is: this is the only other
    thing that writes the flash the code is running from, and a sketch that
    also mounted an SD card would hide which layer failed.
    """
    if serial is None:
        pytest.skip("pyserial is not installed")
    return Board("fatfstest")
```

and add `"fatfs_board"` to the fixture-name tuple at line ~322 that groups tests by image, so all its tests run against one flash.

- [ ] **Step 7: Write the failing FTL test**

Create `tests/hw/test_ftl.py`:

```python
"""The flash translation layer, on its own, before any filesystem uses it.

FAT rewrites its allocation table and directory entries constantly and both
live at fixed sector numbers, so mapped straight onto flash those erase blocks
take every write in the filesystem. The FTL spreads them, and it also turns
8 KB erase pages into the 512-byte LBAs FatFs and USB mass storage expect.

Tested alone because a bug in here surfaces as FAT corruption two layers up,
where it looks like anything but a mapping bug.
"""
import pytest

from conftest import kv


@pytest.fixture(scope="module")
def ftl(fatfs_board):
    fatfs_board.command("ftlcreate", timeout=5)
    assert kv(fatfs_board.command("ftlformat", timeout=30))["ftl_format"] == 1
    assert kv(fatfs_board.command("ftlstart", timeout=10))["ftl_start"] == 1
    return fatfs_board


def test_geometry_matches_the_partition(ftl):
    """The FTL's usable size must be the partition minus its fixed reserve.

    3 erase blocks for garbage collection and 2 for metadata. At 256 KB with
    8 KB pages that is 32 - 5 = 27 blocks of 16 LBAs = 432.
    """
    info = kv(ftl.command("info", timeout=5))
    ebs = info["fs_size"] // info["eb_bytes"]
    assert info["ftl_ebs"] == ebs, info.raw
    assert info["ftl_lbas"] == (ebs - 5) * (info["eb_bytes"] // 512), info.raw


def test_written_lbas_read_back(ftl):
    for lba in (0, 1, 17, 100):
        assert kv(ftl.command(f"ftlwrite {lba}", timeout=10))["ftl_write"] == 1
    for lba in (0, 1, 17, 100):
        r = kv(ftl.command(f"ftlverify {lba}", timeout=10))
        assert r["ftl_read"] == 1 and r["ftl_match"] == 1, r.raw


def test_rewriting_an_lba_replaces_it(ftl):
    """The log-structured case: the new copy must win, not the old one."""
    assert kv(ftl.command("ftlrewrite 17", timeout=10))["ftl_write"] == 1
    r = kv(ftl.command("ftlreverify 17", timeout=10))
    assert r["ftl_match"] == 1, r.raw
    r = kv(ftl.command("ftlverify 0", timeout=10))
    assert r["ftl_match"] == 1, "rewriting one LBA disturbed another"


def test_survives_garbage_collection(ftl):
    """Write one LBA far more times than the partition has erase blocks.

    The FTL has to reclaim space to keep going, which is the operation that
    moves live data between blocks -- the one most likely to lose it.
    """
    assert kv(ftl.command("ftlchurn 200", timeout=120))["ftl_churn"] == 1
    r = kv(ftl.command("ftlverify 0", timeout=10))
    assert r["ftl_match"] == 1, "garbage collection lost an untouched LBA"


def test_the_map_survives_a_reboot(ftl):
    """persist() then reboot then start(): the mapping must come back.

    Without this the filesystem is intact until the first power cycle, which
    is the worst possible time to find out.
    """
    assert kv(ftl.command("ftlpersist", timeout=30))["ftl_persist"] == 1
    # reboot(), not a bare reset: it waits for the boot banner and the prompt,
    # so the command below cannot race the board coming back up.
    ftl.reboot(timeout=10)
    assert kv(ftl.command("ftlstart", timeout=30))["ftl_start"] == 1
    r = kv(ftl.command("ftlverify 100", timeout=10))
    assert r["ftl_match"] == 1, r.raw
```

`kv` currently lives in `tests/hw/test_littlefs.py`. Move it to `conftest.py` unchanged and re-import it there (`from conftest import Reply, _sync, kv`), so both test modules share one parser.

- [ ] **Step 8: Run it and watch it fail for the right reason**

```bash
WLINK=tools/bin/wlink.exe python -m pytest tests/hw/test_ftl.py -q
```

Expected before the sketch builds: a build failure naming `SPIFTL.h`. Expected once it builds but before Step 2's constants are right: `test_geometry_matches_the_partition` fails with a wrong `ftl_lbas`. Both are informative; a pass at this point means the test is not reaching the hardware.

- [ ] **Step 9: Make it pass**

Fix what the failures name. The likely one: the board menu default is 128 KB, and `fatfstest/platformio.ini` needs `board_build.filesystem_size = 262144` for the 256 KB minimum this work targets. Add it with a comment saying why.

- [ ] **Step 10: Commit**

```bash
git add -A
git commit -m "A wear-levelling flash translation layer under the FAT volume

SPIFTL, ported from arduino-pico with its copyright intact. One change of
substance: the erase-block size is a constructor parameter, because upstream
hardcodes 4096 in nine places and this part erases 8192 (4096 on the 480 KB
variant).

Tested on its own, before any filesystem sits on it. FAT rewrites its
allocation table and directory entries at fixed sector numbers, so the FTL is
what stops those erase blocks taking every write in the filesystem -- and a
bug in it would otherwise appear two layers up as FAT corruption with no
obvious cause. The tests cover the log-structured rewrite, garbage collection
moving live data, and the mapping surviving a reboot.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 3: The flash filesystem

**Files:**
- Create: `libraries/FatFS/src/FatFS.h`, `libraries/FatFS/src/FatFS.cpp`
- Create: `libraries/FatFS/examples/ListFiles/ListFiles.ino`
- Modify: `tests/sketches/fatfstest/src/main.cpp`, `tests/hw/test_ftl.py` (unchanged), new `tests/hw/test_fatfs.py`

**Interfaces:**
- Consumes: `SPIFTL`, `CH32H4FTLFlash` from Task 2; the registry from Task 1.
- Produces: `FatFSConfig` with `setAutoFormat(bool)`; `extern FS FatFS;`; `FatFSImpl::begin()`, `::end()`, `::format()`, `::info(FSInfo&)`; `bool FatFSImpl::lbaRead(uint32_t lba, void *dst)` and `::lbaWrite(uint32_t lba, const void *src)` and `::lbaCount()` for Task 4's MSC callbacks; `::mounted()`.

- [ ] **Step 1: Write `FatFS.h`**

Model it on `libraries/SDFS/src/SDFS.h` — same `FSImpl` subclass shape, same `FileImpl`/`DirImpl` pattern — with these differences. The `FSId` is `0x46415446` ("FATF").

```cpp
/* FAT on the internal flash partition.
 *
 *     FatFS.begin();
 *     File f = FatFS.open("/log.txt", "w");
 *
 * THE SAME PARTITION LITTLEFS USES. _FS_start.._FS_end holds one filesystem,
 * and whichever formatted it owns it: flashing a FatFS sketch over a LittleFS
 * one discards that LittleFS, and the reverse is equally true. That is the
 * cost of one partition serving both, and it is what a sketch author expects
 * -- the sketch on the board declares which filesystem the board has.
 *
 * 256 KB MINIMUM, set from the board's filesystem-size menu. Below that the
 * translation layer's fixed reserve leaves under 90 KB, which is a legal FAT12
 * volume and a coin-toss on a Windows host over USB. begin() refuses and says
 * so rather than producing one.
 *
 * Why a translation layer at all: FAT rewrites its allocation table and
 * directory entries constantly and both live at fixed sector numbers, so on
 * raw flash those erase blocks wear out while the rest of the partition is
 * untouched. It also turns 8 KB erase pages into the 512-byte sectors FatFs
 * and USB mass storage expect.
 */
#pragma once

#include <Arduino.h>
#include <FS.h>
#include <FSImpl.h>

#include "ff.h"
#include "SPIFTL.h"
#include "ch32h4_ftl_flash.h"

namespace fatfs {

class FatFSConfig : public FSConfig {
public:
    static constexpr uint32_t FSId = 0x46415446;   /* "FATF" */
    FatFSConfig(bool autoFormat = true) : FSConfig(FSId, autoFormat) { }
    FatFSConfig setAutoFormat(bool val = true) {
        _autoFormat = val;
        return *this;
    }
};

class FatFSImpl : public FSImpl {
public:
    /* The smallest partition this will mount. See the note at the top. */
    static constexpr uint32_t MIN_PARTITION = 256u * 1024u;

    FatFSImpl() { }

    FileImplPtr open(const char *path, OpenMode openMode,
                     AccessMode accessMode) override;
    bool exists(const char *path) override;
    DirImplPtr openDir(const char *path) override;
    bool rename(const char *pathFrom, const char *pathTo) override;
    bool info(FSInfo &info) override;
    bool remove(const char *path) override;
    bool mkdir(const char *path) override;
    bool rmdir(const char *path) override;
    bool stat(const char *path, FSStat *st) override;
    bool setConfig(const FSConfig &cfg) override;
    bool begin() override;
    void end() override;
    bool format() override;

    bool mounted() const { return _mounted; }

    /* Why the last begin() failed.
     *
     * begin() returning false has three quite different causes here, and the
     * core's convention is that diagnostics compile out unless the build asks
     * for them (-DFS_DEBUG) -- so a sketch built normally would get a bare
     * false. This is the machine-readable half: it costs one byte, it is what
     * the hardware tests assert on, and it is what lets a sketch print
     * something useful without the filesystem pulling stdio into every build.
     */
    enum Error : uint8_t {
        ERR_NONE = 0,
        ERR_TOO_SMALL,      /* partition below MIN_PARTITION */
        ERR_NO_VOLUME,      /* nothing mountable, and autoFormat is off */
        ERR_FORMAT_FAILED,  /* the format attempt itself failed */
        ERR_FTL,            /* the translation layer would not start */
    };
    Error lastError() const { return _err; }
    const char *lastErrorString() const;

    /* Block access, for FatFSUSB. A host that has mounted this volume owns
     * its structure, so mass storage goes to the translation layer directly
     * and never through FatFs -- whose cached FAT and directory sectors would
     * otherwise disagree with what the host wrote. */
    uint32_t lbaCount();
    bool lbaRead(uint32_t lba, void *dst);
    bool lbaWrite(uint32_t lba, const void *src);
    bool lbaSync();

private:
    friend class FatFSFileImpl;
    friend class FatFSDirImpl;

    bool startFTL();

    FATFS _fs = {};
    FatFSConfig _cfg;
    CH32H4FTLFlash *_flash = nullptr;
    SPIFTL *_ftl = nullptr;
    bool _mounted = false;
    Error _err = ERR_NONE;
};

}  // namespace fatfs

extern FS FatFS;
using fatfs::FatFSConfig;
```

Copy `SDFSFileImpl` and `SDFSDirImpl` from `SDFS.h` into `FatFS.h` as `FatFSFileImpl`/`FatFSDirImpl`, changing only the class names and the `_fs` type. They are pure FatFs and volume-agnostic.

- [ ] **Step 2: Write the disk driver for volume 0**

At the top of `libraries/FatFS/src/FatFS.cpp`:

```cpp
#include "FatFS.h"
#include "ch32h4_fatfs_disk.h"

/* The core's convention, copied from cores/ch32h4/FS.cpp: diagnostics are
 * compiled out unless the build asks for them, because a printf in the
 * filesystem path would pull stdio into every sketch that opens a file.
 * Build with -DFS_DEBUG to see the messages below -- and they are worth
 * seeing, since "FatFS.begin() returned false" has three quite different
 * causes here. */
#ifndef DEBUGV
#ifdef FS_DEBUG
#define DEBUGV(fmt, ...)  ::printf(fmt, ##__VA_ARGS__)
#else
#define DEBUGV(...)       do { } while (0)
#endif
#endif

extern "C" char _FS_start[];
extern "C" char _FS_end[];

/* FatFs volume 0. See ch32h4_fatfs_disk.h for the numbering. */
#define FLASH_VOL "0:"

using namespace fatfs;

/* The mounted instance's FTL, for the C disk callbacks below. There is one
   flash volume, so one pointer rather than a lookup. */
static SPIFTL *s_ftl = nullptr;

static DSTATUS flash_status(void) {
    return s_ftl ? 0 : STA_NOINIT;
}

static DSTATUS flash_initialize(void) {
    return flash_status();
}

static DRESULT flash_read(BYTE *buff, LBA_t sector, UINT count) {
    if (!s_ftl) {
        return RES_NOTRDY;
    }
    for (UINT i = 0; i < count; i++) {
        if (!s_ftl->read((int)(sector + i), buff + i * 512)) {
            return RES_ERROR;
        }
    }
    return RES_OK;
}

static DRESULT flash_write(const BYTE *buff, LBA_t sector, UINT count) {
    if (!s_ftl) {
        return RES_NOTRDY;
    }
    for (UINT i = 0; i < count; i++) {
        if (!s_ftl->write((int)(sector + i), buff + i * 512)) {
            return RES_ERROR;
        }
    }
    return RES_OK;
}

static DRESULT flash_ioctl(BYTE cmd, void *buff) {
    if (!s_ftl) {
        return RES_NOTRDY;
    }
    switch (cmd) {
        case CTRL_SYNC:
            /* Push the mapping to flash. Without this the filesystem is
             * intact until the first power cycle, and then is not. */
            return s_ftl->persist() ? RES_OK : RES_ERROR;
        case GET_SECTOR_COUNT:
            *(LBA_t *)buff = s_ftl->lbaCount();
            return RES_OK;
        case GET_SECTOR_SIZE:
            *(WORD *)buff = 512;
            return RES_OK;
        case GET_BLOCK_SIZE:
            /* In sectors. The translation layer already spreads writes, so
             * f_mkfs has nothing useful to align to. */
            *(DWORD *)buff = 1;
            return RES_OK;
        default:
            return RES_PARERR;
    }
}

static const ch32h4_fatfs_disk_ops s_flash_ops = {
    flash_status, flash_initialize, flash_read, flash_write, flash_ioctl
};
```

- [ ] **Step 3: Write `begin()`, with the size floor and the LittleFS notice**

```cpp
bool FatFSImpl::startFTL() {
    if (_ftl) {
        return true;
    }
    const uint32_t base = (uint32_t)(uintptr_t)_FS_start;
    const uint32_t len  = (uint32_t)(_FS_end - _FS_start);
    _flash = new CH32H4FTLFlash(base, len);
    _ftl = new SPIFTL(_flash, (int)_flash->ebBytes());
    if (!_ftl->start()) {
        /* No usable mapping. Either the partition has never held a FAT
         * volume, or it holds something else -- LittleFS, most likely. Say
         * which, because "mount failed" sends people looking at the hardware.
         */
        return false;
    }
    return true;
}

bool FatFSImpl::begin() {
    if (_mounted) {
        return true;
    }

    _err = ERR_NONE;

    const uint32_t len = (uint32_t)(_FS_end - _FS_start);
    if (len < MIN_PARTITION) {
        _err = ERR_TOO_SMALL;
        DEBUGV("FatFS: the filesystem partition is %lu bytes; FatFS needs at "
               "least %lu. Raise it with the board's filesystem-size menu "
               "(board_build.filesystem_size under PlatformIO).\n",
               (unsigned long)len, (unsigned long)MIN_PARTITION);
        return false;
    }

    const bool ftlOk = startFTL();
    s_ftl = _ftl;
    ch32h4_fatfs_register_disk(CH32H4_FATFS_PDRV_FLASH, &s_flash_ops);

    if (ftlOk && FR_OK == f_mount(&_fs, FLASH_VOL, 1)) {
        _mounted = true;
        return true;
    }

    if (!_cfg._autoFormat) {
        _err = ERR_NO_VOLUME;
        DEBUGV("FatFS: no FAT volume here and autoFormat is off.\n");
        return false;
    }
    /* The partition belongs to whichever sketch is flashed, so this reformats
     * rather than refusing -- a sketch that called begin() has said which
     * filesystem it wants. Loud about it, because the thing being discarded
     * may be a LittleFS someone put files in. */
    DEBUGV("FatFS: no usable FAT volume in the flash partition; formatting. "
           "Any LittleFS that was here is gone.\n");
    if (!format()) {
        _err = ERR_FORMAT_FAILED;
        return false;
    }
    _mounted = (FR_OK == f_mount(&_fs, FLASH_VOL, 1));
    if (!_mounted) {
        _err = ERR_FORMAT_FAILED;
    }
    return _mounted;
}

const char *FatFSImpl::lastErrorString() const {
    switch (_err) {
        case ERR_NONE:          return "ok";
        case ERR_TOO_SMALL:     return "partition too small, needs 256K";
        case ERR_NO_VOLUME:     return "no FAT volume, autoformat off";
        case ERR_FORMAT_FAILED: return "format failed";
        case ERR_FTL:           return "flash translation layer failed";
    }
    return "unknown";
}

void FatFSImpl::end() {
    if (!_mounted) {
        return;
    }
    f_mount(nullptr, FLASH_VOL, 0);
    if (_ftl) {
        _ftl->persist();
    }
    ch32h4_fatfs_register_disk(CH32H4_FATFS_PDRV_FLASH, nullptr);
    s_ftl = nullptr;
    _mounted = false;
}

bool FatFSImpl::format() {
    if (!_ftl) {
        if (!_flash) {
            _flash = new CH32H4FTLFlash((uint32_t)(uintptr_t)_FS_start,
                                        (uint32_t)(_FS_end - _FS_start));
            _ftl = new SPIFTL(_flash, (int)_flash->ebBytes());
        }
    }
    /* The translation layer first: its metadata is what f_mkfs's sectors will
       land on, and formatting FAT onto a stale mapping produces a filesystem
       that mounts once. */
    if (!_ftl->format() || !_ftl->start()) {
        return false;
    }
    s_ftl = _ftl;
    ch32h4_fatfs_register_disk(CH32H4_FATFS_PDRV_FLASH, &s_flash_ops);

    BYTE *work = (BYTE *)malloc(FF_MAX_SS);
    if (!work) {
        return false;
    }
    /* FM_SFD: no partition table. A single-volume device with the boot sector
       at LBA 0 is what a host expects from something this size. */
    MKFS_PARM opt = { FM_FAT | FM_SFD, 1, 0, 0, 0 };
    FRESULT r = f_mkfs(FLASH_VOL, &opt, work, FF_MAX_SS);
    free(work);
    return r == FR_OK && _ftl->persist();
}
```

The remaining `FatFSImpl` methods (`open`, `exists`, `openDir`, `rename`, `info`, `remove`, `mkdir`, `rmdir`, `stat`, `setConfig`) are the `SDFSImpl` ones from `SDFS.cpp` with `SD_VOL` replaced by `FLASH_VOL` and the class names changed. `info()` reports `f_getfree(FLASH_VOL, ...)`.

At the bottom of the file:

```cpp
FS FatFS(FSImplPtr(new FatFSImpl()));
```

- [ ] **Step 4: Add the block accessors for Task 4**

```cpp
uint32_t FatFSImpl::lbaCount() {
    return _ftl ? (uint32_t)_ftl->lbaCount() : 0;
}

bool FatFSImpl::lbaRead(uint32_t lba, void *dst) {
    return _ftl && _ftl->read((int)lba, dst);
}

bool FatFSImpl::lbaWrite(uint32_t lba, const void *src) {
    return _ftl && _ftl->write((int)lba, src);
}

bool FatFSImpl::lbaSync() {
    return _ftl && _ftl->persist();
}
```

- [ ] **Step 5: Extend the test sketch**

Add to `tests/sketches/fatfstest/src/main.cpp`: `#include <FatFS.h>`, and commands `fsbegin`, `fsend`, `fsformat`, `fsinfo`, `fswrite <name>`, `fsread <name>`, `fsexists <name>`, mirroring the `sdfstest` sketch's key=value replies (`fs_mount=`, `fs_total_kb=`, `fs_used_kb=`, `fs_rt_size=`, …). Add `fsnoautoformat`, which calls `FatFS.setConfig(FatFSConfig().setAutoFormat(false))` before `begin()`.

Every reply that reports a mount must also report the reason, since a bare 0 is
untestable:

```cpp
static void reportMount(bool ok) {
  Serial1.print("fs_mount="); Serial1.println(ok ? 1 : 0);
  /* The string, not the enum: a test asserting fs_err==2 breaks silently the
     day someone inserts a value, and the string is what a person reading the
     log needs anyway. */
  Serial1.print("fs_err=");
  Serial1.println(((fatfs::FatFSImpl *)FatFS.impl())->lastErrorString());
}
```

If `FS` has no `impl()` accessor, keep a file-scope `fatfs::FatFSImpl` pointer
in the sketch rather than adding one to the core — the sketch is a test
harness, and widening a core API for it is the wrong trade.

- [ ] **Step 6: Write the failing filesystem test**

Create `tests/hw/test_fatfs.py` covering, in this order — a failed mount looks identical whether the FTL or FatFs rejected it, and separating them is the only way to tell without a debugger:

```python
def test_mounts_after_format(fatfs_board):
    assert kv(fatfs_board.command("fsformat", timeout=60))["fs_format"] == 1
    assert kv(fatfs_board.command("fsbegin", timeout=30))["fs_mount"] == 1


def test_reports_a_plausible_size(fatfs_board):
    """216 KB usable from a 256 KB partition: 32 erase blocks less the FTL's
    fixed 5, times 8 KB."""
    info = kv(fatfs_board.command("fsinfo", timeout=10))
    assert 200 <= info["fs_total_kb"] <= 220, info.raw


def test_a_file_survives_a_reboot(fatfs_board):
    assert kv(fatfs_board.command("fswrite hello", timeout=30))["fs_rt"] == "ok"
    fatfs_board.reboot(timeout=10)
    assert kv(fatfs_board.command("fsbegin", timeout=30))["fs_mount"] == 1
    r = kv(fatfs_board.command("fsread hello", timeout=30))
    assert r["fs_rt"] == "ok", r.raw


def test_this_image_is_above_the_minimum(fatfs_board):
    """The fixture builds at 256 KB, so this asserts the precondition rather
    than the refusal. The refusal path is Step 10, by hand, once."""
    info = kv(fatfs_board.command("info", timeout=5))
    assert info["fs_size"] >= 256 * 1024


def test_autoformat_off_refuses_rather_than_reformatting(fatfs_board):
    """With autoFormat off, an unformatted partition must fail and say why.

    The error code matters more than the false: begin() has three quite
    different failure causes, and a sketch built without -DFS_DEBUG sees no
    message at all.
    """
    assert kv(fatfs_board.command("ftlformat", timeout=30))["ftl_format"] == 1
    r = kv(fatfs_board.command("fsnoautoformat", timeout=30))
    assert r["fs_mount"] == 0, r.raw
    assert r["fs_err"] == "no FAT volume, autoformat off", r.raw
```

- [ ] **Step 7: Run, fix, re-run**

```bash
WLINK=tools/bin/wlink.exe python -m pytest tests/hw/test_ftl.py tests/hw/test_fatfs.py -q
```

Both files must pass. Task 2's tests must still pass — `test_fatfs.py` formats the same partition, so if the two interleave badly that is a real ordering bug in the fixtures, not a flake to retry.

- [ ] **Step 8: Verify the size-floor refusal by hand, once**

Automating this needs a second image differing only in a build flag, which is
not worth a flash cycle on every run. Do it once and record the output:

```bash
cd tests/sketches/fatfstest
# temporarily: board_build.filesystem_size = 131072
pio run -t upload
# then, over Serial1:  fsbegin
```

Expected: `fs_mount=0` and `fs_err=partition too small, needs 256K`. Restore
`262144` afterwards and confirm `fsbegin` succeeds again. Paste both replies
into the commit message.

- [ ] **Step 9: Write the example**

Create `libraries/FatFS/examples/ListFiles/ListFiles.ino`: mount, write a file if absent, list the directory with sizes, print `FSInfo`. Head comment states the 256 KB minimum and that this partition is shared with LittleFS.

```bash
python tools/buildexamples.py FatFS
python tools/buildexamples.py --ide FatFS
```

- [ ] **Step 10: Commit**

```bash
git add -A
git commit -m "FAT on the internal flash, on volume 0

FatFSImpl over the translation layer, with the same FSImpl shape as SDFS so
File and Dir behave identically on both volumes.

Two decisions worth naming. begin() refuses a partition under 256 KB and says
which menu setting to raise: below that the FTL's fixed five-erase-block
reserve leaves under 90 KB, a legal FAT12 volume and a coin-toss on a Windows
host. And it auto-formats otherwise, matching LittleFSConfig and FSConfig here
-- the partition belongs to whichever sketch is flashed, so a sketch that
called begin() has declared which filesystem the board has. It says out loud
that it is discarding whatever was there.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 4: USB mass storage

**Files:**
- Create: `libraries/FatFSUSB/library.properties`, `src/FatFSUSB.h`, `src/FatFSUSB.cpp`, `examples/USBDrive/USBDrive.ino`
- Modify: `docs/hazards.md`

**Interfaces:**
- Consumes: `FatFSImpl::lbaCount/lbaRead/lbaWrite/lbaSync` from Task 3.
- Produces: `extern FatFSUSBClass FatFSUSB;` with `begin()`, `end()`, `onPlug()`, `onUnplug()`, `driveReady()`.

- [ ] **Step 1: Port the API surface**

Take `arduino-pico/libraries/FatFSUSB/src/FatFSUSB.{h,cpp}`, keep the LGPL header and attribution, and add the CH32H41x port line. Replace upstream's raw `tusb-msc.h` descriptor plumbing with `Adafruit_USBD_MSC`, which this core already has and which owns the descriptor here. The callbacks map:

| upstream | here |
|---|---|
| `tud_msc_read10_cb` | `Adafruit_USBD_MSC::setReadWriteCallback` read lambda |
| `tud_msc_write10_cb` | the same callback's write half |
| `tud_msc_capacity_cb` | `setCapacity(blockCount, 512)` |
| `tud_msc_test_unit_ready_cb` | `setReadyCallback` |
| `tud_msc_start_stop_cb` | `setStartStopCallback`, which is where plug/unplug fire |

- [ ] **Step 2: Route the block callbacks at the FTL, not FatFs**

```cpp
int32_t FatFSUSBClass::read10(uint32_t lba, uint32_t offset, void *buffer, uint32_t bufsize) {
    /* Straight to the translation layer. While a host has this volume
     * mounted it owns the filesystem structure, and going through FatFs would
     * hand back its cached FAT and directory sectors -- which the host has
     * already changed underneath. */
    if (offset != 0 || bufsize % 512) {
        return -1;
    }
    uint8_t *p = (uint8_t *)buffer;
    for (uint32_t i = 0; i < bufsize / 512; i++) {
        if (!_fs->lbaRead(lba + i, p + i * 512)) {
            return -1;
        }
    }
    return (int32_t)bufsize;
}
```

and the mirror for `write10`, ending with `_fs->lbaSync()` when the host issues its final sync.

- [ ] **Step 3: Wire the ownership contract**

`onPlug` fires when the host mounts, `onUnplug` on eject. Document in the header, in these words or close to them:

```cpp
/* THE SKETCH AND THE HOST MUST NOT BOTH HAVE THE VOLUME MOUNTED.
 *
 * FatFs caches FAT and directory sectors; a host writing underneath that
 * cache corrupts one or both views, and does it silently. The contract:
 * onPlug -> the sketch calls FatFS.end(); onUnplug -> FatFS.begin() and
 * re-read anything it cares about. driveReady() lets the sketch refuse the
 * host while it is mid-operation, which reports "not ready" -- something
 * every host handles.
 *
 * There is no way to share a FAT volume between two writers without a
 * locking protocol neither side has. */
```

- [ ] **Step 4: Measure the USB stall**

`write10` runs in the TinyUSB task and calls into flash; our flash write parks the other core and masks interrupts for the duration of an 8 KB erase. Add to the example sketch a timer around the write callback that records the worst case and prints it on demand, copy a 64 KB file from a host, and read the figure.

- [ ] **Step 5: Record it in `docs/hazards.md`**

Under a new heading, with the measured number — not an estimate. If the worst case exceeds roughly 100 ms, say so and note the fallback in the spec (buffer the sector in RAM in the callback, commit from `loop()`), which is then a follow-up task rather than something to improvise here.

- [ ] **Step 6: Verify against real hosts**

Not automatable. Enumerate on Windows, macOS and Linux; copy a file each way on each; eject and confirm `onUnplug` fires and the sketch can re-mount and read what the host wrote. Record the results in the commit message.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "FatFSUSB: the flash volume as a USB stick

Adafruit_USBD_MSC over the flash volume's translation layer, bypassing FatFs
-- while a host has the volume mounted it owns the filesystem structure, and
FatFs's cached FAT and directory sectors would disagree with what the host
wrote.

The ownership contract is upstream's and is the only workable one: onPlug ->
the sketch calls FatFS.end(), onUnplug -> begin() again. Two writers cannot
share a FAT volume without a locking protocol neither side has, and the
failure mode without the contract is silent corruption.

Worst-case USB stall from a flash erase inside the write callback: <measured>
ms, recorded in docs/hazards.md. Verified enumerating and copying both ways on
Windows, macOS and Linux.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Task 5: Both volumes at once

The case the whole restructure exists for, and the one nothing so far has tested: flash and SD mounted simultaneously.

**Files:**
- Create: `tests/sketches/twovoltest/platformio.ini`, `src/main.cpp`, `tests/hw/test_two_volumes.py`
- Modify: `tests/hw/conftest.py`, `docs/hazards.md`, `libraries/FatFS/README` note if one exists

- [ ] **Step 1: Write the sketch**

`tests/sketches/twovoltest/src/main.cpp` mounts both and offers `both`, `write <vol> <name>`, `read <vol> <name>`, `interleave <n>`. `board_build.filesystem_size = 262144` in its `platformio.ini`. The SD card must be wired per `sd_board`'s note (CK PC12, CMD PD2, D0 PC8); tests skip without one.

- [ ] **Step 2: Add the fixture**

```python
@pytest.fixture(scope="session")
def two_volume_board():
    """Flash and SD mounted at once -- what the FatFs restructure is for.

    Neither the fatfs_board nor the fs_board image can show this: each mounts
    one volume, and a single-volume FatFs would pass both of their suites while
    failing the moment a second volume existed.
    """
    if serial is None:
        pytest.skip("pyserial is not installed")
    return Board("twovoltest")
```

- [ ] **Step 3: Write the test**

```python
def test_both_mount_together(two_volume_board):
    r = kv(two_volume_board.command("both", timeout=60))
    if r.get("sd_present") == 0:
        pytest.skip("no SD card wired")
    assert r["flash_mount"] == 1 and r["sd_mount"] == 1, r.raw


def test_interleaved_writes_do_not_cross_volumes(two_volume_board):
    """The failure this guards against is a single-volume FatFs quietly
    serving both mounts -- which passes every single-volume test."""
    r = kv(two_volume_board.command("interleave 32", timeout=120))
    assert r["flash_match"] == 1, r.raw
    assert r["sd_match"] == 1, r.raw
```

- [ ] **Step 4: Run the whole hardware suite**

```bash
WLINK=tools/bin/wlink.exe python -m pytest tests/hw -q
```

Expected: everything that passed before this work still passes, plus the new files. Do not run this concurrently with any other use of the board — two things driving the probe is what produced a long run of spurious failures previously.

- [ ] **Step 5: Run the static suite and every example**

```bash
python -m pytest tests/ -q --ignore=tests/hw
python tools/buildexamples.py
python tools/buildexamples.py --ide
```

- [ ] **Step 6: Commit**

```bash
git add -A
git commit -m "Test the thing the restructure was for: both volumes at once

A single-volume FatFs quietly serving whichever filesystem mounted first
passes every test in test_fatfs.py and test_filesystem.py, because each
mounts one volume. This is the test that fails.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>"
```

---

## Self-review notes

Checked against the spec:

- Structure, registry, volume numbering, `ffconf.h` table → Task 1.
- FTL port, `ebBytes` parameterisation, capacity arithmetic → Task 2.
- `FatFSImpl`, 256 KB floor, auto-format, LittleFS notice → Task 3.
- FatFSUSB, ownership contract, USB stall measurement → Task 4.
- Two-volume test, full-suite regression → Task 5.
- Spec's "out of scope" items (SD as a second LUN, replacing LittleFS, SD API changes) appear in no task, correctly.

Type consistency: `ch32h4_fatfs_disk_ops` field order is fixed in Task 1 Step 2 and both `s_sd_ops` (Task 1) and `s_flash_ops` (Task 3) use it positionally in that order. `SPIFTL(FlashInterface*, int)` is defined in Task 2 Step 2 and called in Task 2 Step 5 and Task 3 Steps 3–4. `lbaRead/lbaWrite/lbaSync/lbaCount` are declared in Task 3 Step 1 and used in Task 4 Step 2.

Four things this review caught and fixed, recorded because they are the kind
of error that survives into an implementation and wastes a day:

1. **`DEBUGV` is not available.** It is defined inside `cores/ch32h4/FS.cpp`
   behind an `#ifndef`/`FS_DEBUG` guard, not in any header, and neither
   `SDFS.cpp` nor `LittleFS.cpp` reports anything at all. Task 3 now repeats
   that guard locally, as the core does.
2. **Which then broke the spec's requirement** that `begin()` say why it
   failed — a message compiled out by default says nothing. Resolved with a
   `FatFSImpl::Error` enum and `lastErrorString()`: machine-readable for the
   tests, printable by a sketch, and costing one byte rather than stdio.
   `begin()` has three genuinely different failure causes and a bare `false`
   cannot distinguish them.
3. **`Board` has no `reset()`.** The harness offers `Board.reboot(timeout)`,
   which waits for the boot banner and prompt, and a module-level `_sync()`.
   Two tests used a method that does not exist.
4. **`MKFS_PARM` field order** verified against `ff.h` as
   `{fmt, n_fat, align, n_root, au_size}` — the plan's initialiser is correct.

One deliberate gap: Task 3's `test_this_image_is_above_the_minimum` asserts the
precondition rather than exercising the refusal, because the fixture builds at
256 KB. The refusal is verified by hand in Task 3 Step 8, once, with the output
pasted into the commit. Automating it needs a second image differing only in a
build flag, which is not worth a flash cycle on every run.

Two risks the implementer should carry, neither resolvable on paper:

- **SPIFTL's `uint8_t buff[flashWriteBufferSize]`** is a variable-length array,
  a GCC extension in C++. It compiles under this toolchain; if a future
  `-pedantic` lands it will not. Not worth changing pre-emptively, worth
  knowing.
- **Every SPIFTL `program()` is 256- or 512-byte aligned**, checked against all
  five call sites, which is what makes `ch32h4_flash_write`'s whole-page
  requirement safe. The `CH32H4FTLFlash::program()` guard rejects anything else
  rather than trusting that survey — if it ever returns false, the survey was
  wrong, and that is a much better failure than a silently corrupt filesystem.
