/* SDFS -- the FS interface, on FatFs, on the SDMMC block driver.
 *
 * The API is arduino-pico's (and esp8266's before it): FS, File, Dir. A sketch
 * written against either works here unchanged, which is the point -- the
 * filesystem API is not somewhere to be creative.
 *
 *     SDFS.setConfig(SDFSConfig(1, 20000000));   // 1-bit, 20 MHz
 *     SDFS.begin();
 *     File f = SDFS.open("/log.txt", "a");
 *
 * Or, for sketches written against the classic Arduino library, <SD.h> in this
 * core is a thin shim over this.
 */
#pragma once

#include <Arduino.h>
#include <FS.h>
#include <FSImpl.h>

extern "C" {
#include "ch32h4_sdmmc.h"
/* FatFs lives in the FatFS library now, not here: one copy serves this volume
 * and the internal flash one. Angle brackets rather than a relative path
 * because it is another library's public header. */
#include <ff.h>

/* Hands this file's disk driver to FatFs as volume 1. Called from
 * SDFSImpl::begin(); see ch32h4_fatfs_disk.h for why not a constructor. */
void ch32h4_sd_disk_register(void);
}

using namespace fs;

class SDFSConfig : public FSConfig {
public:
    static constexpr uint32_t FSId = 0x53444653;   /* "SDFS" */

    /* width is 1 or 4 data lines; freq is a ceiling and the driver reports
     * what it actually reached. The defaults are the ones measured good on
     * this board: 1-bit because that is what the header brings out, and
     * 20 MHz because it is comfortably inside default speed. */
    SDFSConfig(uint8_t width = 1, uint32_t freq = 20000000)
        : FSConfig(FSId, false), _width(width), _freq(freq) { }

    SDFSConfig setAutoFormat(bool val = true) {
        _autoFormat = val;
        return *this;
    }
    SDFSConfig setWidth(uint8_t w) { _width = w; return *this; }
    SDFSConfig setFreq(uint32_t f) { _freq = f; return *this; }

    uint8_t _width;
    uint32_t _freq;
};

class SDFSImpl : public FSImpl {
public:
    SDFSImpl() { }

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

    /* What the driver negotiated. Worth exposing: a card that quietly declined
     * high speed is otherwise invisible from up here. */
    uint32_t freq() const { return ch32h4_sd.freq; }
    uint8_t width() const { return ch32h4_sd.width; }
    uint64_t size() const {
        return (uint64_t)ch32h4_sd.block_count * CH32H4_SD_BLOCK_SIZE;
    }

    bool mounted() const { return _mounted; }

private:
    friend class SDFSFileImpl;
    friend class SDFSDirImpl;

    FATFS _fs = {};
    bool _mounted = false;
    uint8_t _width = 1;
    uint32_t _freq = 20000000;
    bool _autoFormat = false;
};

extern FS SDFS;
