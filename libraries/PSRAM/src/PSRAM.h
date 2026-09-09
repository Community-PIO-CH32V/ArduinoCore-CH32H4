/* 8 MB of pseudo-SRAM on QSPI2.
 *
 *     PSRAM.begin();
 *     PSRAM.write(off, src, len);
 *     PSRAM.read(off, dst, len);
 *
 * The chip is an ESP-PSRAM64H, which is an AP Memory APS6404L die. It is
 * wired to PE10..PE15 and those pins reach QSPI2 on AF7 -- not QSPI1, whose
 * bank sits elsewhere on the package with the clock on a different AF from
 * the data.
 *
 * EVERY TRANSFER IS DMA, AT ANY SIZE OR ALIGNMENT. There is no fast path and
 * no slow path to know about: read() and write() take any address, any length
 * and any alignment, and all of them run at the full line rate -- 12.45 MB/s
 * unaligned against 12.48 aligned, which is close enough to stop thinking
 * about it.
 *
 * That holds because only the MEMORY side constrains DMA. A caller's pointer
 * has to be word-aligned and the transfer a whole number of words; the PSRAM
 * side does not care, since the device is byte-addressed and a ragged byte
 * count is set exactly by DLR. So an unaligned buffer costs one word of
 * staging at each end -- enough to bring the pointer to a word boundary and
 * to mop up the remainder -- and the bulk in between goes straight to or from
 * the caller's memory with no copy at all.
 *
 * THERE IS DELIBERATELY NO data() POINTER. An earlier version exposed the
 * controller's memory-mapped window as a live const uint8_t *, which made
 * random access a plain CPU load. It was removed because it could not be made
 * to work at every clock: at 50 MHz a long memcpy through that window returns
 * corrupt data, while every DMA transfer is clean. An API that hands out a
 * pointer which is only valid for short reads, at one clock, is a worse thing
 * to own than a function call -- so the window is gone and the clock went up
 * instead. Bulk copying through that window was never the fast way anyway: it
 * managed 2.9 MB/s against DMA's 12.5 at the same clock.
 *
 * The cost is random access. Indexing a structure now means a read() call of
 * a few microseconds rather than a pointer dereference of a few hundred
 * nanoseconds. Read a struct once into RAM, work on it there, write it back.
 *
 * ON THE CLOCK: 25 MHz, and it is not negotiable on current evidence. QSPI
 * divides HCLK at 100 MHz -- the V5F's 400 MHz never reaches this peripheral
 * -- and 25 MHz is divider 4.
 *
 * 50 MHz was tried and rejected, which is worth knowing before trying it
 * again. Sweeping lengths from 1 byte to 1 KB across a range of addresses
 * showed it clean, and it is not: a 64-byte read at address 0 comes back with
 * everything from byte 32 onward slipped by one nibble. 32 bytes is the FIFO
 * depth. It is address- and pattern-dependent, so a sweep that misses the
 * wrong address calls it clean -- which is exactly what happened.
 *
 * The ceiling is also not monotonic, so a lower clock is not automatically
 * safer and a higher one not merely slower: reads fail at divider 3
 * (33.3 MHz) and pass at divider 2 and divider 1. Anything that changes this
 * clock needs the whole matrix, several addresses included, not one sketch
 * that appears to work. See docs/qspi-read-timing.md and
 * tests/hw/test_psram_clock_matrix.py.
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

    /* Any address, any length, any alignment; all of it by DMA.
       Both clamp to the device and return bytes actually transferred, so
       running off the end gives a short count rather than a wrapped address
       quietly corrupting the bottom of the array. 0 before begin(). */
    size_t read(uint32_t addr, void *dst, size_t len);
    size_t write(uint32_t addr, const void *src, size_t len);


    /* How long the last write() took, end to end -- including the two mode
       flips, which a large write amortises and a small one does not. */
    uint32_t lastWriteMicros() const { return _lastWriteUs; }

private:
    bool identify();
    bool readDMA(uint32_t addr, uint8_t *dst, uint32_t len);
    bool xfer(uint8_t ins, uint32_t addr, bool hasAddr, uint8_t *rx,
              const uint8_t *tx, uint32_t len, int lines, int dummy);
    bool writeDMA(uint32_t addr, const uint8_t *src, uint32_t len,
                  uint32_t req);
    uint32_t _lastWriteUs = 0;
    bool _begun = false;
    bool _detected = false;
    uint8_t _mfid = 0;
    uint8_t _kgd = 0;
    uint8_t _eid[6] = {0};
    uint32_t _clock = 0;
};

extern PSRAMClass PSRAM;
