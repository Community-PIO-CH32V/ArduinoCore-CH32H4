/* SD.h -- the classic Arduino SD library API, over SDFS.
 *
 * A thin shim, the way arduino-pico does it. Sketches written against the
 * stock Arduino SD library compile and run unchanged; sketches that want the
 * richer API use <SDFS.h> directly. Both drive the same filesystem, so mixing
 * them in one sketch is fine.
 *
 * The begin() overloads differ from the stock library's for a reason worth
 * stating: the classic API takes a chip-select pin because it assumes SD over
 * SPI. This part has a real SDMMC controller, so there is no CS -- begin()
 * takes a bus width and a clock instead, and the pin-taking overloads exist
 * only so an unmodified sketch still compiles. They ignore the pin.
 */
#pragma once

#include <Arduino.h>
#include <FS.h>
#include <SDFS.h>

/* The stock library's mode constants. FILE_WRITE means append, which surprises
 * people, but changing it would break every sketch that logs. */
#undef FILE_READ
#define FILE_READ  "r"
#undef FILE_WRITE
#define FILE_WRITE "a"

class SDClass {
public:
    /* The native form: bus width and clock. */
    bool begin(uint8_t width = 1, uint32_t freq = 20000000) {
        SDFS.setConfig(SDFSConfig(width, freq));
        return SDFS.begin();
    }

    /* Compatibility only. The stock library's argument is a chip-select pin
     * for SD-over-SPI; there is no CS on the SDMMC controller, so it is
     * accepted and ignored rather than silently doing something else with it.
     * A sketch that passes a pin here gets the default bus. */
    bool begin(int csPin) {
        (void)csPin;
        return begin();
    }

    void end() {
        SDFS.end();
    }

    File open(const char *path, const char *mode = FILE_READ) {
        return SDFS.open(path, mode);
    }
    File open(const String &path, const char *mode = FILE_READ) {
        return open(path.c_str(), mode);
    }

    bool exists(const char *path) { return SDFS.exists(path); }
    bool exists(const String &path) { return SDFS.exists(path.c_str()); }

    bool remove(const char *path) { return SDFS.remove(path); }
    bool remove(const String &path) { return SDFS.remove(path.c_str()); }

    bool rename(const char *from, const char *to) {
        return SDFS.rename(from, to);
    }
    bool rename(const String &from, const String &to) {
        return SDFS.rename(from.c_str(), to.c_str());
    }

    bool mkdir(const char *path) { return SDFS.mkdir(path); }
    bool mkdir(const String &path) { return SDFS.mkdir(path.c_str()); }

    bool rmdir(const char *path) { return SDFS.rmdir(path); }
    bool rmdir(const String &path) { return SDFS.rmdir(path.c_str()); }

    /* The card, not the filesystem. A sketch that wants to know whether the
     * card is bigger than the volume on it needs both. */
    uint64_t size64() {
        return (uint64_t)ch32h4_sd.block_count * CH32H4_SD_BLOCK_SIZE;
    }
    uint32_t size() { return (uint32_t)size64(); }

    /* Stock-library spellings, in the units it used. */
    uint8_t type() { return ch32h4_sd.card_type; }
    uint32_t clusterCount() {
        FSInfo info;
        return SDFS.info(info) ? (uint32_t)(info.totalBytes / info.blockSize) : 0;
    }
    uint64_t totalBytes() {
        FSInfo info;
        return SDFS.info(info) ? info.totalBytes : 0;
    }
    uint64_t usedBytes() {
        FSInfo info;
        return SDFS.info(info) ? info.usedBytes : 0;
    }

    bool format() { return SDFS.format(); }
};

extern SDClass SD;
