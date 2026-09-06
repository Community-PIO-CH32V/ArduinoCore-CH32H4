/* FatFS -- the FS interface, on FatFs, on the internal flash.
 *
 * The API is arduino-pico's (and esp8266's before it): FS, File, Dir. The same
 * one SDFS presents, so code that works against one works against the other.
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
 * 256 KB MINIMUM, from the board's filesystem-size menu
 * (board_build.filesystem_size under PlatformIO). Below that the translation
 * layer's fixed reserve -- three erase blocks for garbage collection, two for
 * metadata -- leaves under 90 KB, which is a legal FAT12 volume and a
 * coin-toss on a Windows host over USB. begin() refuses and lastErrorString()
 * says why, rather than producing one.
 *
 * Why a translation layer at all: FAT rewrites its allocation table and
 * directory entries constantly and both live at fixed sector numbers, so on
 * raw flash those erase blocks would wear out while the rest of the partition
 * stayed untouched. It also turns this part's 8 KB erase pages into the
 * 512-byte sectors FatFs and USB mass storage expect.
 */
#pragma once

#include <Arduino.h>
#include <FS.h>
#include <FSImpl.h>

#include "SPIFTL.h"
#include "ch32h4_ftl_flash.h"

extern "C" {
#include <ff.h>
}

using namespace fs;

class FatFSConfig : public FSConfig {
public:
    static constexpr uint32_t FSId = 0x46415446;   /* "FATF" */

    /* autoFormat defaults TRUE, matching LittleFSConfig and FSConfig in this
     * core. The partition belongs to whichever sketch is flashed: a sketch
     * that called begin() has declared which filesystem it wants, and should
     * get a working one rather than a mount failure on a board that has never
     * held a FAT volume. Pass false to fail instead of reformatting. */
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

private:
    friend class FatFSFileImpl;
    friend class FatFSDirImpl;

    bool startFTL();

    FATFS _fs = {};
    bool _mounted = false;
    bool _autoFormat = true;
};

extern FS FatFS;

/* The translation layer under this volume.
 *
 * THERE IS EXACTLY ONE, and this is how to reach it. Two SPIFTL objects over
 * the same partition would each hold their own logical-to-physical map in RAM
 * and each believe it authoritative, so a write through one would be invisible
 * to the other and the second would happily allocate over it. Anything wanting
 * block-level access -- a test, a tool, FatFSUSB -- must come through here
 * rather than construct its own.
 *
 * Created on first call if it does not exist yet; started (its map restored
 * from flash) only by FatFS.begin() or FatFS.format(). */
SPIFTL *ch32h4_fatfs_ftl();

/* ---- Why begin() said no --------------------------------------------------
 *
 * begin() returning false has four quite different causes, and this core's
 * convention is that filesystem diagnostics compile out unless the build asks
 * for them (-DFS_DEBUG) -- so a sketch built normally would otherwise get a
 * bare false. These cost one byte of state and let a sketch print something
 * useful without the filesystem dragging stdio into every build.
 */
extern "C" {

enum ch32h4_fatfs_error {
    CH32H4_FATFS_OK = 0,
    CH32H4_FATFS_ERR_TOO_SMALL,      /* partition below MIN_PARTITION */
    CH32H4_FATFS_ERR_NO_VOLUME,      /* nothing mountable, autoFormat off */
    CH32H4_FATFS_ERR_FORMAT_FAILED,  /* the format attempt itself failed */
    CH32H4_FATFS_ERR_FTL,            /* translation layer would not start */
};

enum ch32h4_fatfs_error ch32h4_fatfs_last_error(void);
const char *ch32h4_fatfs_last_error_string(void);

/* ---- Block access, for FatFSUSB -------------------------------------------
 *
 * A host that has mounted this volume owns its structure, so mass storage
 * goes to the translation layer directly and never through FatFs -- whose
 * cached FAT and directory sectors would otherwise disagree with whatever the
 * host just wrote.
 *
 * A C API rather than methods on FatFSImpl because FS::getImpl() is protected
 * with a single friend, and widening the core's interface for one library is
 * the wrong trade. These operate on whichever FTL FatFS last started, and
 * report zero or false when it has not been started at all.
 */
/* How many CTRL_SYNC flushes FatFs has asked for, and how many failed.
 * "The file vanished after a reboot" has two very different causes -- FatFs
 * never asking, or the layer failing to write -- and they look identical from
 * outside. */
uint32_t ch32h4_fatfs_sync_count(void);
uint32_t ch32h4_fatfs_sync_failures(void);

uint32_t ch32h4_fatfs_lba_count(void);
bool ch32h4_fatfs_lba_read(uint32_t lba, void *dst);
bool ch32h4_fatfs_lba_write(uint32_t lba, const void *src);
bool ch32h4_fatfs_lba_sync(void);

}  // extern "C"
