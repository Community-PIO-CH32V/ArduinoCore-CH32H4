/* LittleFS on the internal flash, in the shape arduino-pico's LittleFS uses.
 *
 *     LittleFS.begin();
 *     File f = LittleFS.open("/log.txt", "a");
 *     f.println("hello");
 *     f.close();
 *
 * Same FS/File/Dir API as SDFS, so a sketch can move between them by changing
 * the object it opens through and nothing else.
 *
 * ### The partition has to be asked for
 *
 * There is no filesystem unless the build reserved one:
 *
 *     board_build.filesystem_size = 128k        ; platformio.ini
 *
 * or the Filesystem menu in the Arduino IDE. The default is zero, because a
 * filesystem nobody asked for is flash taken away from every sketch that does
 * not want one. begin() returns false when the region is empty and says so on
 * the debug console rather than failing silently -- an empty region and a
 * corrupt one are very different problems and should not look the same.
 *
 * The region sits immediately below the EEPROM, which is pinned to the last
 * 16 KB of flash. Resizing the filesystem therefore never moves the EEPROM,
 * and a stored setting survives a rebuild -- the same reason arduino-pico
 * pins its EEPROM to the top of flash.
 *
 * ### Wear, and why the block size is 8 KB
 *
 * The flash erase page on this part is 8 KB (4 KB on the 480 KB part), and a
 * LittleFS block cannot be smaller than the erase unit. That is large as
 * LittleFS blocks go, so a small partition has few blocks: 64 KB is eight of
 * them, which works but leaves little room for the log-structured writes to
 * spread wear across. 128 KB or more is a more comfortable size, and the
 * driver refuses fewer than four blocks outright because LittleFS cannot keep
 * its two metadata pairs plus data below that.
 */
#pragma once

#include <Arduino.h>
#include <FS.h>
#include <FSImpl.h>

extern "C" {
#include "ch32h4_console.h"
#include "ch32h4_flash.h"
#include "littlefs/lfs.h"
}

using namespace fs;

class LittleFSConfig : public FSConfig {
public:
    static constexpr uint32_t FSId = 0x4C465331;   /* "LFS1" */

    LittleFSConfig(bool autoFormat = true) : FSConfig(FSId, autoFormat) { }

    LittleFSConfig setAutoFormat(bool val = true) {
        _autoFormat = val;
        return *this;
    }
};

class LittleFSImpl : public FSImpl {
public:
    LittleFSImpl() { }
    ~LittleFSImpl() { end(); }

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
    bool gc() override;

    bool mounted() const { return _mounted; }

    /* Where the partition is, for a sketch that wants to report it. Zero size
     * means the build reserved none. */
    uint32_t partitionStart() const;
    uint32_t partitionSize() const;
    uint32_t blockSize() const { return _block_size; }

private:
    friend class LittleFSFileImpl;
    friend class LittleFSDirImpl;

    bool configure();

    /* The block device, as LittleFS wants it. Static rather than member
     * functions because lfs_config takes plain function pointers; the instance
     * arrives through cfg->context. */
    static int lfs_read(const struct lfs_config *c, lfs_block_t block,
                        lfs_off_t off, void *buffer, lfs_size_t size);
    static int lfs_prog(const struct lfs_config *c, lfs_block_t block,
                        lfs_off_t off, const void *buffer, lfs_size_t size);
    static int lfs_erase(const struct lfs_config *c, lfs_block_t block);
    static int lfs_sync(const struct lfs_config *c);

    lfs_t _lfs = {};
    struct lfs_config _cfg = {};
    bool _mounted = false;
    bool _autoFormat = true;

    uint32_t _start = 0;
    uint32_t _size = 0;
    uint32_t _block_size = 0;

    /* LittleFS's working buffers, so it does not malloc them. Sized from
     * _cfg below; static extents keep the heap out of the mount path, which
     * matters because a mount failure that is really an allocation failure is
     * indistinguishable from a corrupt filesystem. */
    static constexpr uint32_t CACHE_SIZE = 256;
    static constexpr uint32_t LOOKAHEAD_SIZE = 32;

    uint8_t _read_buf[CACHE_SIZE] __attribute__((aligned(4)));
    uint8_t _prog_buf[CACHE_SIZE] __attribute__((aligned(4)));
    uint8_t _lookahead_buf[LOOKAHEAD_SIZE] __attribute__((aligned(4)));
};

extern FS LittleFS;
