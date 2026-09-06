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
    /* 64-bit, so a large eb cannot wrap the multiply back into range. */
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
     * caller never supplied over data it did not mention.
     *
     * Every SPIFTL write is writeBufferSize() or lbaBytes, both multiples of
     * 256, so this should never trip. It is checked rather than assumed
     * because the alternative failure is a silently corrupt filesystem: if it
     * ever returns false here, the survey of SPIFTL's call sites was wrong,
     * and that is much better learned from a failed write than from a file
     * that reads back as garbage a week later. */
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
