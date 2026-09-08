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
 * RANDOM ACCESS IS FREE; BULK COPYING IS NOT. After begin() the controller
 * rests in memory-mapped mode, so data() is a live pointer and indexing it is
 * an ordinary CPU load with no library involvement at all. Memory-mapped mode
 * is read-only in hardware -- that is the design of this controller, not an
 * omission -- so every write has to abort out of it, transfer, and re-enter.
 *
 * USE data() FOR RANDOM ACCESS AND read() FOR BULK. They are not the same
 * speed, and not in the direction you would guess: the window is unbeatable
 * per access but only reaches ~2.9 MB/s when copied from in a loop, because
 * discrete CPU loads do not keep the controller streaming. read() runs DMA
 * instead and sustains 12.5 MB/s -- the full line rate, and over 4x a memcpy
 * from the window. So `memcpy(dst, PSRAM.data() + off, len)` is the slow way
 * to do what `PSRAM.read(off, dst, len)` does.
 *
 * THREE CONSEQUENCES, all real:
 *
 *   1. data() MUST NOT be dereferenced while read() or write() is running.
 *      Both leave memory-mapped mode to do their work. A single-threaded
 *      sketch cannot hit this, because they return before anything else runs.
 *      Touching it from an interrupt WILL return rubbish.
 *   2. Transfers cost a mode flip -- about 5 us. Batch into few large calls
 *      rather than many small ones.
 *   3. DMA needs a word-aligned pointer and a whole number of words. Anything
 *      else still works, on the slow path, silently. If bulk throughput
 *      matters, align the buffer.
 *
 * THE CLOCK IS 25 MHz AND SHOULD STAY THERE. QSPI divides HCLK at 100 MHz --
 * the V5F's 400 MHz never reaches this peripheral -- and 25 MHz (divider 4) is
 * the only setting measured clean on every test.
 *
 * It is NOT a simple "faster is worse" ceiling, and that matters if you are
 * tempted to raise it. Reads fail at divider 3 (33.3 MHz) but pass at divider
 * 2 (50 MHz) and divider 1 (100 MHz); writes fail only at divider 2. The
 * corruption has integer structure -- exactly 32 bytes wrong at divider 3
 * regardless of transfer length, exactly half the bytes wrong at divider 2 --
 * so it is a data-path fault, not analog marginality, and the mechanism is not
 * understood. 100 MHz very nearly works: it streams a 64 KB read at 49 MB/s,
 * but with 2 corrupt words in 16384, which is far too many for a pointer you
 * dereference without checking.
 *
 * So: do not raise this on the strength of one sketch that appears to work.
 * Several settings pass some tests and fail others. See
 * docs/qspi-read-timing.md and tests/hw/test_psram_clock_matrix.py.
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

    /* QSPI2's memory-mapped window. QSPI1's is at 0x90000000 and is NOT this
       one -- reading there returns zeros. */
    static const uint32_t MMAP_BASE = 0x70000000u;

    /* The device as ordinary memory, or nullptr when the window is not live.
     *
     * Safe to hold across calls, but see the header note: it must not be
     * dereferenced while write() is running, because a write has to leave
     * memory-mapped mode to do its job.
     *
     * The nullptr case is deliberately checked against _mapped and not just
     * _begun. Reading this window while the controller is not actually in
     * memory-mapped mode does not return rubbish -- it hangs the CPU on an AHB
     * access that never completes and never faults, which also locks out the
     * debug probe. A null pointer is a far better failure than that. */
    const uint8_t *data() const {
        return (_begun && _mapped) ? (const uint8_t *)MMAP_BASE : nullptr;
    }

    bool mapped() const { return _mapped; }

    /* How long the last write() took, end to end -- including the two mode
       flips, which a large write amortises and a small one does not. */
    uint32_t lastWriteMicros() const { return _lastWriteUs; }

    /* Benchmark hook: force the DMA read path regardless of the threshold.
       read() picks between DMA and the window on its own; this exists so the
       crossover between them can be re-measured on another board, which is
       where PSRAM_READ_DMA_THRESHOLD comes from. 0 unless word-aligned. */
    size_t readViaDMA(uint32_t addr, void *dst, size_t len);

private:
    bool identify();
    bool readDMA(uint32_t addr, uint8_t *dst, uint32_t len);
    bool xfer(uint8_t ins, uint32_t addr, bool hasAddr, uint8_t *rx,
              const uint8_t *tx, uint32_t len, int lines, int dummy);
    bool writeDMA(uint32_t addr, const uint8_t *src, uint32_t len,
                  uint32_t req);
    void mapEnter();
    void mapExit();

    uint32_t _lastWriteUs = 0;
    bool _mapped = false;
    bool _begun = false;
    bool _detected = false;
    uint8_t _mfid = 0;
    uint8_t _kgd = 0;
    uint8_t _eid[6] = {0};
    uint32_t _clock = 0;
};

extern PSRAMClass PSRAM;
