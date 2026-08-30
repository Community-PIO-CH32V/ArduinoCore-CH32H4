/* EEPROM emulation in the flash tail.
 *
 * This part has no EEPROM, so the last 16 KB of the 960 KB user area stands in
 * for one. Two things about the flash make the obvious implementation wrong.
 *
 * ERASE GRANULARITY IS 8 KB. A 4 KB Arduino-style EEPROM cannot be one sector,
 * and read-modify-write of a single sector has a window -- between the erase
 * and the rewrite -- in which a power cut loses everything. So two pages are
 * used alternately: commit() writes the inactive one and only then invalidates
 * the old, and there is no instant at which no valid copy exists.
 *
 * ERASED FLASH READS 0xE339E339, not 0xFFFFFFFF. Every "is this blank?" test
 * has to use that, and any code that assumes all-ones decides erased flash is
 * full of data. It is the single most likely thing to get wrong here.
 *
 * The region is cut out of the FLASH_V5F linker region, so an image that grows
 * into it is a link error rather than a board that works until someone calls
 * commit().
 */
#pragma once

#include <Arduino.h>

/* The flash tail. Must match the end of FLASH_V5F in the variant's linker
 * script: they are two statements of one boundary and nothing checks them
 * against each other at build time. */
#define CH32H4_EEPROM_BASE       0x080EC000u
#define CH32H4_EEPROM_PAGE_SIZE  0x2000u        /* erase granularity, DBMODE=1 */
#define CH32H4_EEPROM_PAGES      2u
#define CH32H4_EEPROM_MAX_SIZE   (CH32H4_EEPROM_PAGE_SIZE - 16u)

/* Erased flash on this part. NOT 0xFFFFFFFF. */
#define CH32H4_FLASH_ERASED_WORD 0xE339E339u

class EEPROMClass {
public:
    /* Arduino's ESP-style API: begin(size), then read/write into a RAM mirror,
     * then commit(). The mirror is why writes are cheap and why nothing
     * reaches flash until you ask. */
    bool begin(size_t size = 4096);
    void end();

    uint8_t read(int address);
    void write(int address, uint8_t value);
    void update(int address, uint8_t value);

    bool commit();
    bool wasCommitted() const { return _dirty == false; }

    /* AVR-style single-byte access, for sketches written against it. These go
     * straight through to the mirror; commit() is still needed. */
    uint8_t operator[](int address) { return read(address); }

    template <typename T> T &get(int address, T &t) {
        if (_mirror && address >= 0 && address + (int)sizeof(T) <= (int)_size) {
            memcpy(&t, _mirror + address, sizeof(T));
        }
        return t;
    }

    template <typename T> const T &put(int address, const T &t) {
        if (_mirror && address >= 0 && address + (int)sizeof(T) <= (int)_size) {
            memcpy(_mirror + address, &t, sizeof(T));
            _dirty = true;
        }
        return t;
    }

    size_t length() const { return _size; }

    /* Which page currently holds the live copy, 0 or 1, or -1 if neither does
     * -- a fresh chip. Exposed because the alternation is the whole design and
     * a test that cannot see it cannot prove it. */
    int activePage() const { return _active; }

private:
    uint32_t pageAddress(int page) const {
        return CH32H4_EEPROM_BASE + (uint32_t)page * CH32H4_EEPROM_PAGE_SIZE;
    }
    int findActivePage() const;

    uint8_t *_mirror = nullptr;
    size_t _size = 0;
    int _active = -1;
    bool _dirty = false;
    uint32_t _serial = 0;
};

extern EEPROMClass EEPROM;
