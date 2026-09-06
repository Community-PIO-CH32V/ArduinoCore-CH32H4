# One FatFs, three volumes: FatFS, SDFS and FatFSUSB

**Status:** design, approved 2026-09-06.

## What this is for

Today ChaN's FatFs is vendored inside `libraries/SDFS/src/fatfs/`, built for a
single volume, and reachable only through `SDFS`. There is no FAT filesystem on
the internal flash, and nothing exposes a filesystem over USB.

The goal is a sketch that can do all of this at once:

```cpp
FatFS.begin();          // FAT on internal flash
FatFSUSB.begin();       // ...visible to a host as a USB stick
SDFS.begin();           // FAT on the SD card, at the same time
```

A user plugs the board into a PC, drags files onto the drive that appears, and
the sketch reads them back — while logging to an SD card that the host never
sees.

## What already exists

- `cores/ch32h4/FS.h` and `FSImpl.h`: the same filesystem abstraction
  arduino-pico uses, so `File`, `Dir` and `FSInfo` are already in place.
- `libraries/SDFS`: `SDFSImpl : FSImpl` over a vendored FatFs, plus
  `ch32h4_diskio.c` bridging to `cores/ch32h4/ch32h4_sdmmc.c`.
- `libraries/SD`: a thin Arduino-compatible wrapper over SDFS. Untouched by
  this work.
- `libraries/LittleFS`: owns `_FS_start.._FS_end`, the internal flash
  filesystem partition, sized by the `board_build.filesystem_size` menu
  (default 128 KB).
- `cores/ch32h4/ch32h4_flash.{h,c}`: `erase`, `write`, `read`, `page_size`
  (8192 on the 960 KB part, 4096 on the 480 KB one), `prog_size` (256). These
  already park the other core and run the register sequences from ITCM.
- `libraries/Adafruit_TinyUSB_Arduino`: `Adafruit_USBD_MSC` and
  `msc_device.c`, compiled when the sketch pulls them in.

## What upstream does, and where we deliberately differ

arduino-pico has libraries with these names but a different arrangement:

| | arduino-pico | here |
|---|---|---|
| `FatFS` | ChaN FatFs on flash, `FF_VOLUMES 1` | ChaN FatFs, `FF_VOLUMES 2` |
| `SDFS` | **SdFat** (Greiman) — a separate codebase | the same ChaN FatFs, volume 1 |
| `FatFSUSB` | MSC over the flash volume | the same |

Upstream never shares FatFs between SD and flash; it carries two independent
FAT implementations. We carry one, built multi-volume. That is the whole point
of the restructure, and it is why the file layout below matches upstream while
the internals do not.

## Structure

```
libraries/FatFS/
  src/ff.c ffunicode.c ff.h ffconf.h    the only copy of ChaN FatFs
  src/diskio.c diskio.h                 dispatch on pdrv through a registry
  src/ch32h4_fatfs_disk.h               the registry's interface
  src/SPIFTL.h                          ported flash translation layer
  src/ch32h4_ftl_flash.{h,cpp}          SPIFTL's FlashInterface on our flash
  src/FatFS.{h,cpp}                     FatFSImpl : FSImpl, volume 0
libraries/SDFS/
  src/SDFS.{h,cpp}                      SDFSImpl : FSImpl, volume 1
  src/ch32h4_sd_disk.c                  registers the SD driver for pdrv 1
libraries/SD/                           unchanged
libraries/FatFSUSB/
  src/FatFSUSB.{h,cpp}                  MSC over volume 0
```

`libraries/SDFS/src/fatfs/` is deleted. `SDFS/library.properties` gains
`depends=FatFS`, `FatFSUSB/library.properties` gains `depends=FatFS`.

### The disk registry

ChaN's `disk_read`/`disk_write`/`disk_ioctl`/`disk_status`/`disk_initialize`
take a `pdrv` and must reach two different drivers. A registry, rather than a
`switch` in one file:

```c
/* ch32h4_fatfs_disk.h */
typedef struct {
    DSTATUS (*status)(void);
    DSTATUS (*initialize)(void);
    DRESULT (*read)(BYTE *buff, LBA_t sector, UINT count);
    DRESULT (*write)(const BYTE *buff, LBA_t sector, UINT count);
    DRESULT (*ioctl)(BYTE cmd, void *buff);
} ch32h4_fatfs_disk_ops;

#define CH32H4_FATFS_PDRV_FLASH  0
#define CH32H4_FATFS_PDRV_SD     1

void ch32h4_fatfs_register_disk(BYTE pdrv, const ch32h4_fatfs_disk_ops *ops);
```

`diskio.c` holds `static const ch32h4_fatfs_disk_ops *s_disk[FF_VOLUMES]`,
returns `STA_NOINIT` for an unregistered `pdrv`, and otherwise forwards.

**This is what keeps the restructure free.** A sketch that uses only SD never
names `FatFS`, so nothing references the FTL or the flash driver and
`--gc-sections` drops both; a sketch that uses only flash never links the SDMMC
driver. Without the registry, a `switch` in `diskio.c` would reference both
drivers unconditionally and every SD sketch would carry the FTL.

Registration happens from each `FSImpl::begin()`, not from a static
constructor: static constructors run for every linked translation unit and
would defeat the collection above.

### Volume numbering and paths

`FF_VOLUMES 2`, `FF_STR_VOLUME_ID 0`. Volume 0 is flash, volume 1 is SD.
The `"0:"`/`"1:"` prefixes are an implementation detail of each `FSImpl`,
which prepends its own before calling FatFs. Sketch-visible paths are
unchanged and unprefixed:

```cpp
FatFS.open("/log.txt", "r");   // -> f_open("0:/log.txt", ...)
SDFS.open("/log.txt", "r");    // -> f_open("1:/log.txt", ...)
```

`ffconf.h` changes, from the current file:

| setting | now | after | why |
|---|---|---|---|
| `FF_VOLUMES` | 1 | 2 | flash and SD mounted together |
| `FF_MIN_SS` | 512 | 512 | unchanged |
| `FF_MAX_SS` | 512 | 512 | the FTL presents 512-byte LBAs, so both volumes agree and FatFs keeps its fixed-sector fast paths |
| `FF_USE_MKFS` | 1 | 1 | unchanged; both volumes can format |
| `FF_FS_REENTRANT` | 0 | 0 | see "Concurrency" |

## The flash block device

### Why a translation layer

FAT rewrites its allocation table and directory entries on nearly every file
operation, and both live at fixed sector numbers. Mapped straight onto flash,
those erase blocks take every write in the filesystem and wear out while the
rest of the partition is untouched. A log-structured translation layer spreads
them.

It also settles the sector size. Our erase page is 8192 bytes; presenting that
as the FatFs sector would force `FF_MAX_SS` to 8192, diverge the two volumes,
and hand USB MSC an 8192-byte block size that not every host handles well. The
FTL presents 512-byte LBAs, which is what every host expects and what SD
already uses.

### The port

`SPIFTL.h` is taken from `arduino-pico/libraries/FatFS/lib/SPIFTL`, keeping its
copyright header, with one change: `ebBytes` becomes a constructor parameter.
Upstream hardcodes `const int ebBytes = 4096;` — ours is 8192, and 4096 on the
480 KB part, so it must come from `ch32h4_flash_page_size()` at run time.

`ch32h4_ftl_flash.cpp` implements SPIFTL's `FlashInterface` over
`ch32h4_flash_erase/write/read`, bounded to `_FS_start.._FS_end`.

### What it costs

The FTL reserves 3 erase blocks for garbage collection and 2 for metadata.
On our geometry, with 8192-byte erase blocks:

| partition | erase blocks | usable FAT volume | overhead | FTL RAM |
|---|---|---|---|---|
| 128 KB (the board default) | 16 | 88 KB | 31% | ~0.4 KB |
| **256 KB (FatFS minimum)** | 32 | 216 KB | 16% | ~0.9 KB |
| 512 KB | 64 | 472 KB | 8% | ~2 KB |

RAM is the logical-to-physical map (`uint16_t` per 512-byte LBA) plus per-erase-block
wear counters. The fixed 5-block reserve is what makes a small partition
expensive and a large one cheap.

**256 KB is a hard minimum for FatFS, enforced in `begin()`.** Below it,
`FatFS.begin()` fails with a message naming the board menu setting to change,
rather than producing an 88 KB FAT12 volume that is legal, unusual, and a
coin-toss on Windows. Refusing is better than shipping a size whose host
compatibility we would have to qualify and then defend.

This does not change the board's 128 KB default, which stays right for
LittleFS. A sketch wanting FatFS selects 256 KB or more from the filesystem
menu, and is told exactly that if it forgets.

## FatFSUSB

Single LUN, volume 0 only, on `Adafruit_USBD_MSC`. The API mirrors upstream:

```cpp
bool begin();
void end();
void onPlug(void (*cb)(uint32_t), uint32_t data = 0);
void onUnplug(void (*cb)(uint32_t), uint32_t data = 0);
void driveReady(bool (*cb)(uint32_t), uint32_t data = 0);
```

MSC `read10`/`write10` go straight to the FTL, bypassing FatFs: the host owns
the filesystem structure while it is mounted, and the board is only a block
device.

### Ownership

**The sketch and the host must never both have the volume mounted.** FatFs
caches directory and FAT sectors; a host writing underneath that cache
corrupts one or both views, silently. The contract, which the examples must
demonstrate:

- `onPlug` fires when the host mounts — the sketch calls `FatFS.end()`.
- `onUnplug` fires on eject — the sketch calls `FatFS.begin()` and re-reads
  whatever it cares about.
- `driveReady` lets the sketch refuse the host while it is mid-operation;
  returning false reports "not ready", which hosts handle.

This is upstream's design and it is right: there is no way to share a FAT
volume between two writers without a locking protocol neither side has.

### The USB stall hazard

`write10` runs in the TinyUSB task and calls into flash. Our flash write parks
the other core and masks interrupts for the duration of an erase — 8 KB on
this part. USB bulk transfers tolerate delay, but the worst-case stall must be
**measured and recorded in `docs/hazards.md`**, not assumed: this is exactly
the class of thing that works against one host controller and fails against
another.

If the stall proves too long, the fallback is to buffer the sector in RAM
inside the callback and commit it from `loop()`. That is a bigger change and is
not in scope unless measurement forces it.

## LittleFS coexistence

Both filesystems map `_FS_start.._FS_end` and are mutually exclusive: whichever
formatted the partition owns it. No linker script or `boards.txt` change.

`FatFS.begin()` **auto-formats an unusable partition by default**, matching
upstream's `FatFSConfig(autoFormat = true)` and LittleFS's existing behaviour.
The partition belongs to whichever sketch is flashed: a sketch that calls
`FatFS.begin()` has declared which filesystem it wants, and it should get a
working one rather than a mount failure on a board that has never held a FAT
volume. `FatFSConfig().setAutoFormat(false)` opts out for a sketch that would
rather fail than reformat.

The consequence, which belongs in the documentation and in the examples:
**flashing a FatFS sketch over a LittleFS one discards the LittleFS
partition**, and the reverse is equally true. That is the cost of one partition
serving both, and it is the behaviour a sketch author expects; it should not be
discovered from a support thread.

Each filesystem still recognises the other's signature and reports *that*
specifically before reformatting, so the log says "reformatting a LittleFS
partition as FAT" rather than a generic mount failure followed by silence.

## Concurrency

`FF_FS_REENTRANT` stays 0. FatFs is not called from two cores: the existing
rule that lwIP belongs to the V5F applies here too, and the V3F has no business
in a filesystem. `FatFSUSB`'s MSC callbacks run on the same core as the sketch.

What the FTL and the flash driver do underneath is already safe: every write
parks the other core.

## Testing

**Static** — the examples build under both PlatformIO and arduino-cli, via
`tools/buildexamples.py`. arduino-cli resolves libraries by the headers at the
top of `src/`, so `FatFS.h`, `SDFS.h` and `FatFSUSB.h` must sit there; the
`fatfs` subdirectory disappearing is part of what makes that work.

**Hardware**, added to `tests/hw`:

1. `test_fatfs.py` — format, mount, write, read back, unmount, remount, and
   confirm the file survives a reboot.
2. `test_fatfs_littlefs.py` — `FatFS.begin()` on a LittleFS partition
   recognises it, says so, reformats, and comes up usable; the reverse too.
   With `setAutoFormat(false)` it refuses instead, with the same specific
   message. Also that `begin()` below 256 KB fails and names the menu setting.
3. `test_two_volumes.py` — **the test this restructure exists for**: flash and
   SD mounted simultaneously, interleaved writes to both, each read back
   independently.
4. FTL wear behaviour: write enough to force garbage collection, then confirm
   the volume is still consistent.

**By hand**, and recorded: USB MSC enumeration on Windows, macOS and Linux, and
file copy in both directions, at the 256 KB minimum and at 512 KB.

## Out of scope

- Exposing the SD card over MSC as a second LUN. Considered and rejected for
  now: it doubles the MSC surface and needs an arbitration scheme between host
  and sketch that is a design in its own right.
- Replacing LittleFS, or running both filesystems at once.
- `SD`/`SDFS` API changes. Existing sketches compile unchanged.

## Order of work

1. Move FatFs into `FatFS/`, add the registry, convert SDFS to volume 1.
   **SDFS keeps working throughout** — this step is a refactor with no new
   behaviour, and the existing SD hardware tests are the proof.
2. Port SPIFTL and the flash interface, and exercise the FTL on hardware
   directly — write and read back LBAs, force garbage collection, remount and
   confirm the mapping survives — before any filesystem sits on it. A bug here
   would otherwise surface as unexplainable FAT corruption two steps later.
3. `FatFSImpl` on volume 0, and the LittleFS signature check.
4. `FatFSUSB`, and measure the USB stall.
5. Examples, and the two-volume hardware test.

Step 1 is separable and safe to land on its own; nothing after it changes SDFS.
