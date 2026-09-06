/* SPIFTL's view of our internal flash.
 *
 * NOT A FLASH DRIVER. The driver is cores/ch32h4/ch32h4_flash.{h,c} -- the
 * same one LittleFS uses, which parks the other core and runs the register
 * sequences from ITCM. This is only an adapter: SPIFTL addresses flash as an
 * erase-block index plus an offset behind a C++ virtual interface, and
 * ch32h4_flash_* takes absolute addresses, so something has to translate. It
 * adds no policy of its own beyond bounds-checking.
 *
 * Bounded to the filesystem partition, so a bug in the translation layer
 * cannot reach the sketch above it or the EEPROM below it.
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

    /* The erase-block size this part actually has: 8192, or 4096 on the
       480 KB variant. SPIFTL takes it as a constructor argument. */
    uint32_t ebBytes() const { return _ebBytes; }

private:
    bool inRange(int eb, int offset, int size) const;

    uint32_t _base;
    uint32_t _len;
    uint32_t _ebBytes;
    uint32_t _progSize;
};
