/* 8 MB of pseudo-SRAM on QSPI2, memory-mapped for reading.
 *
 *     PSRAM.begin();
 *     memcpy(dst, PSRAM.data() + off, len);   // an ordinary load
 *     PSRAM.write(off, src, len);             // goes through the library
 *
 * The chip is an ESP-PSRAM64H, which is an AP Memory APS6404L die. It is
 * wired to PE10..PE15 and those pins reach QSPI2 on AF7 -- not QSPI1, whose
 * bank sits elsewhere on the package with the clock on a different AF from
 * the data.
 *
 * READS ARE FREE, WRITES ARE NOT. After begin() the controller rests in
 * memory-mapped mode, so reading is a CPU load with no library involvement at
 * all and random access costs nothing extra. Memory-mapped mode is read-only
 * in hardware -- that is the design of this controller, not an omission -- so
 * every write has to abort out of it, transfer, and re-enter.
 *
 * TWO CONSEQUENCES, both real:
 *
 *   1. data() MUST NOT be dereferenced while write() is running. A
 *      single-threaded sketch cannot hit this, because write() returns before
 *      anything else runs. Reading from an interrupt while the main loop
 *      writes WILL return rubbish.
 *   2. A write costs a mode flip. Batch into few large write() calls rather
 *      than many small ones.
 *
 * THE CLOCK IS 25 MHz AND SHOULD STAY THERE. QSPI divides HCLK at 100 MHz --
 * the V5F's 400 MHz never reaches this peripheral -- and above 25 MHz reads
 * fail because the data coming back misses its sampling window. Writes
 * survive higher clocks, because a write has no round trip, but this driver
 * uses one clock for both. Raising it needs a measurement, not optimism.
 */
#pragma once

#include <Arduino.h>

class PSRAMClass {
public:
    static const size_t CAPACITY = 8u * 1024u * 1024u;
    static const uint32_t DEFAULT_CLOCK = 25000000u;

    /* Start the controller and identify the chip. False if the clock is
       unreachable, the chip does not answer, or this was already begun. */
    bool begin(uint32_t clockHz = DEFAULT_CLOCK);
    bool end();

    /* CAPACITY once begun, 0 before. */
    size_t size() const { return _begun ? CAPACITY : 0; }

    /* Whether the last begin() saw a valid ID. Differs from begin()'s return
       value only after a failure: it says the chip did not answer, as against
       the clock being unusable. */
    bool detected() const { return _detected; }

    uint8_t manufacturerID() const { return _mfid; }
    void eid(uint8_t out[6]) const;

    /* The clock actually being used, which is HCLK divided by an integer and
       so is usually a little under what was asked for. */
    uint32_t clock() const { return _clock; }

    /* Both clamp to the device and return bytes actually transferred, so
       running off the end gives a short count rather than a wrapped address
       quietly corrupting the bottom of the array. 0 before begin(). */
    size_t read(uint32_t addr, void *dst, size_t len);
    size_t write(uint32_t addr, const void *src, size_t len);

private:
    bool identify();
    bool xfer(uint8_t ins, uint32_t addr, bool hasAddr, uint8_t *rx,
              const uint8_t *tx, uint32_t len, int lines, int dummy);

    bool _begun = false;
    bool _detected = false;
    uint8_t _mfid = 0;
    uint8_t _kgd = 0;
    uint8_t _eid[6] = {0};
    uint32_t _clock = 0;
};

extern PSRAMClass PSRAM;
