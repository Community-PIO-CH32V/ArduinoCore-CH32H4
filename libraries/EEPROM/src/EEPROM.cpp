#include "EEPROM.h"

/* Each page starts with a header:
 *
 *   magic    identifies a page this library wrote
 *   serial   increments on every commit; the higher one is the live copy
 *   size     how many bytes of payload follow
 *   check    a simple sum, so a half-written page is not mistaken for a good one
 *
 * The serial is what makes the alternation work. Both pages may be valid at
 * once -- that is the normal state straight after a commit -- and the newer
 * one wins. Nothing is ever erased before its replacement is complete, so
 * there is no instant at which a power cut loses the contents.
 */
struct EEPROMHeader {
    uint32_t magic;
    uint32_t serial;
    uint32_t size;
    uint32_t check;
};

#define EEPROM_MAGIC  0x43483445u   /* "CH4E" */

static uint32_t checksum(const uint8_t *data, size_t len, uint32_t serial) {
    uint32_t sum = 0x9E3779B9u ^ serial;
    for (size_t i = 0; i < len; i++) {
        sum = (sum << 1) | (sum >> 31);
        sum += data[i];
    }
    return sum;
}

int EEPROMClass::findActivePage() const {
    int best = -1;
    uint32_t best_serial = 0;

    for (int p = 0; p < (int)CH32H4_EEPROM_PAGES; p++) {
        const EEPROMHeader *h = (const EEPROMHeader *)pageAddress(p);

        /* An erased page reads 0xE339E339, not 0xFFFFFFFF. A blank check
         * against all-ones would decide erased flash is full of data. */
        if (h->magic == CH32H4_FLASH_ERASED_WORD || h->magic != EEPROM_MAGIC) {
            continue;
        }
        if (h->size == 0 || h->size > CH32H4_EEPROM_MAX_SIZE) {
            continue;
        }

        const uint8_t *payload = (const uint8_t *)(pageAddress(p) + sizeof(EEPROMHeader));
        if (checksum(payload, h->size, h->serial) != h->check) {
            /* Half-written: the erase and the header landed but the payload
             * did not, or the power went during the write. Ignore it -- the
             * other page still holds the previous good copy. */
            continue;
        }

        if (best < 0 || h->serial > best_serial) {
            best = p;
            best_serial = h->serial;
        }
    }
    return best;
}

bool EEPROMClass::begin(size_t size) {
    if (size == 0 || size > CH32H4_EEPROM_MAX_SIZE) {
        return false;
    }
    if (_mirror && _size == size) {
        return true;
    }

    free(_mirror);
    _mirror = (uint8_t *)malloc(size);
    if (!_mirror) {
        return false;
    }
    _size = size;

    _active = findActivePage();
    if (_active < 0) {
        /* Fresh chip, or nothing valid. Arduino's convention is that unwritten
         * EEPROM reads as 0xFF, and sketches rely on it, so the mirror starts
         * that way regardless of what the flash actually reads. */
        memset(_mirror, 0xFF, _size);
        _serial = 0;
    } else {
        const EEPROMHeader *h = (const EEPROMHeader *)pageAddress(_active);
        const uint8_t *payload =
            (const uint8_t *)(pageAddress(_active) + sizeof(EEPROMHeader));
        const size_t n = (h->size < _size) ? h->size : _size;
        memcpy(_mirror, payload, n);
        if (n < _size) {
            memset(_mirror + n, 0xFF, _size - n);
        }
        _serial = h->serial;
    }

    _dirty = false;
    return true;
}

void EEPROMClass::end() {
    if (_dirty) {
        commit();
    }
    free(_mirror);
    _mirror = nullptr;
    _size = 0;
}

uint8_t EEPROMClass::read(int address) {
    if (!_mirror || address < 0 || (size_t)address >= _size) {
        return 0xFF;
    }
    return _mirror[address];
}

void EEPROMClass::write(int address, uint8_t value) {
    if (!_mirror || address < 0 || (size_t)address >= _size) {
        return;
    }
    if (_mirror[address] != value) {
        _mirror[address] = value;
        _dirty = true;
    }
}

void EEPROMClass::update(int address, uint8_t value) {
    write(address, value);   /* write() already skips an unchanged byte */
}

bool EEPROMClass::commit() {
    if (!_mirror) {
        return false;
    }
    if (!_dirty) {
        return true;   /* nothing to do; do not burn an erase cycle */
    }

    /* Write to the page that is NOT live, so the live one stays intact until
     * the new copy is complete. That is the whole reason there are two. */
    const int target = (_active == 0) ? 1 : 0;
    const uint32_t addr = pageAddress(target);
    const uint32_t serial = _serial + 1;

    EEPROMHeader h;
    h.magic = EEPROM_MAGIC;
    h.serial = serial;
    h.size = _size;
    h.check = checksum(_mirror, _size, serial);

    if (!ch32h4_flash_erase(addr, CH32H4_EEPROM_PAGE_SIZE)) {
        return false;
    }

    /* Programmed a PAGE at a time, not a word.
     *
     * This used to use FLASH_ProgramWord() and it did not work. The SDK's
     * word programming leaves the first word of a run correct and the rest
     * wrong on this silicon -- commit() returned true and the data did not
     * survive, which nothing here noticed because the EEPROM had no hardware
     * test at all. ch32h4_flash_write() uses the fast page program, which is
     * the path that works; see ch32h4_flash.c for the two things about it
     * that are not optional.
     *
     * The unit is 256 bytes, so the header shares its page with the first
     * 240 bytes of payload and cannot be committed separately.
     *
     * ORDER STILL MATTERS, and it is now the reverse of what it was. Every
     * page EXCEPT the first goes down first, and the page carrying the header
     * goes last. A power cut therefore leaves a page whose magic never
     * arrived, which findActivePage() ignores, so the other page stays live
     * with the old contents -- rather than a page that announces itself valid
     * and carries a partial payload.
     */
    const uint32_t prog = ch32h4_flash_prog_size();
    const uint32_t total = (uint32_t)(sizeof(EEPROMHeader) + _size);
    const uint32_t pages = (total + prog - 1u) / prog;

    /* Assembled a page at a time out of the header and the mirror, so no
     * buffer the size of the whole EEPROM is needed. Erased flash is left
     * where there is nothing to write. */
    uint8_t buf[256];
    for (uint32_t page = pages; page-- > 0; ) {
        const uint32_t off = page * prog;
        memset(buf, 0xFF, sizeof(buf));

        for (uint32_t i = 0; i < prog; i++) {
            const uint32_t pos = off + i;
            if (pos < sizeof(EEPROMHeader)) {
                buf[i] = ((const uint8_t *)&h)[pos];
            } else if (pos - sizeof(EEPROMHeader) < _size) {
                buf[i] = _mirror[pos - sizeof(EEPROMHeader)];
            }
        }

        if (!ch32h4_flash_write(addr + off, buf, prog)) {
            return false;
        }
    }

    _active = target;
    _serial = serial;
    _dirty = false;
    return true;
}

EEPROMClass EEPROM;
