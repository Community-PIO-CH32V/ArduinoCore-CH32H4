# 8 MB of PSRAM, memory-mapped

**Status:** design, approved 2026-09-06.

## What this is for

An ESP-PSRAM64H sits on a breakout wired to PE10–PE15. It is 64 Mbit (8 MB) of
pseudo-SRAM behind a QSPI front end — nine times the part's entire 896 KB of
internal SRAM. This makes it usable from a sketch:

```cpp
PSRAM.begin();
memcpy(buf, PSRAM.data() + offset, len);   // reads are ordinary loads
PSRAM.write(offset, src, len);             // writes go through the library
```

Two uses drive the design and both were asked for: **bulk streaming buffers**
(audio staging, ahead of the ESP8266Audio port) and **random-access
structures** the sketch indexes directly. The second is what forces
memory-mapping — a function call per field access is not random access.

## Everything below was measured, not assumed

The bench probe (`scratchpad/psram_probe`) established all of this on the
actual board. No part of this design rests on the datasheet where the bench
disagreed with it.

| Fact | Value |
|---|---|
| Peripheral | **QSPI2**, **AF7**, all six pins uniformly |
| Pins | SCLK PE10, CE# PE11, IO0 PE12, IO1 PE13, IO2 PE14, IO3 PE15 |
| Chip identity | MFID `0x0D` (AP Memory), KGD `0x5D`, EID `46 A1 22 34 AE 8B` |
| Memory-mapped window | **`0x70000000`** |
| Quad read | `0xEB`, **exactly 6 dummy cycles**, correct at **≤25 MHz** |
| Quad write | `0x38`, no dummy cycles |
| Sustained read throughput | **12.50 MB/s** — 100% of the 25 MHz × 4-bit line rate |

**`QSPI_EnableQuad()` is mandatory and undocumented.** It sets `SIOXEN` in
`CR`, which gates IO2/IO3 entirely. Without it every 4-line phase fails while
single-line phases keep working — a failure that looks exactly like a
signal-integrity wall and is not one. This cost a full frequency sweep of
false negatives before it was found.

**25 MHz is a controller limit, not a board limit.** Quad reads fail above it
regardless of dummy-cycle count, and adding dummy cycles rescues nothing —
which rules out simple late-data. STM32's QUADSPI absorbs round-trip delay
with a sample-shift bit (`CR` bit 4); on this part that bit position is
undefined in WCH's header and setting it changes nothing measurable. There is
no knob to turn. Separately, single-line reads fail above 33 MHz because
`0x02`/`0x03` are the APS6404L's own slow commands — also not the board.

**GPIO slew made no measurable difference** across Low/Medium/High/Very_High
at every clock tested. Worth recording anyway: this part has an
STM32F4-style `GPIOx->SPEED` register in addition to the F1-style mode
register, and **the core's `ch32h4_pin_af()` never writes it**, so every
core-configured pin sits at its reset value. That is a latent issue for SPI
and I2S too and gets its own `hazards.md` entry. This library sets it
directly via the SDK's `GPIO_Init` rather than relying on the core helper.

## tCEM, and why `data()` can be a plain pointer

The APS6404L is DRAM internally and refreshes itself only while CE# is high,
so the datasheet caps CE#-low time at **8 µs**. Taken literally that forbids
memory-mapped bulk reads outright: a 64 KB `memcpy` holds CE# low for
milliseconds and nothing in a pointer-based API can prevent it.

Measured instead:

| burst | CE# low | errors in the burst | errors in 48 witness rows elsewhere |
|---|---|---|---|
| 64 B | 21 µs | 0 | 0 |
| 4 KB | 329 µs | 0 | 0 |
| 64 KB | **5244 µs** | **0** | **0** |

655× over the limit, no loss anywhere in the array. The throughput is the
corroboration that CE# genuinely stayed low: 12.50 MB/s is exactly the line
rate, and a controller re-issuing the command per access would have paid
20 clocks of overhead per 4 bytes and come in roughly 3.5× slower.

**So `data()` is a normal `const uint8_t *`.** Two caveats are recorded rather
than buried: this is one chip at room temperature, and retention margin
shrinks as temperature rises. The datasheet limit exists to be guaranteed
across the full range, and this measurement does not make violating it *safe* —
it makes it *observed-benign here*. The library enables the `LPTR` timeout
anyway, which costs nothing and bounds CE# during idle.

## The API

`libraries/PSRAM/src/PSRAM.{h,cpp}`, one singleton, since there is one chip on
one peripheral.

```cpp
class PSRAMClass {
public:
    bool begin(uint32_t clockHz = 25000000);
    bool end();

    size_t size() const;              // 8*1024*1024, or 0 before begin()

    /* Whether the most recent begin() saw a valid ID. Distinct from
       begin()'s own return value only after a FAILED begin(): it says the
       chip did not answer, as against the rate being unusable or begin()
       having been called twice. */
    bool detected() const;

    size_t read(uint32_t addr, void *dst, size_t len);
    size_t write(uint32_t addr, const void *src, size_t len);

    const uint8_t *data() const;      // 0x70000000, or nullptr before begin()

    uint8_t manufacturerID() const;   // 0x0D
    void eid(uint8_t out[6]) const;   // per-die serial
};
extern PSRAMClass PSRAM;
```

`begin()` returns false, changes nothing, and leaves `size()` at 0 if the ID
read does not answer `0x0D`/`0x5D`. That check is free — it is the same
transaction that proves the wiring — so there is no separate probe call and no
way to have a "successfully" started PSRAM that is not there.

**The resting state is memory-mapped.** After `begin()`, QSPI2 sits in
memory-mapped quad-read mode permanently, so `data()` is live and a read is an
ordinary CPU load with no library involvement at all.

**Writes flip modes.** Memory-mapped mode is read-only — that is inherent to
the design, not an omission — so `write()` aborts out of it, performs an
indirect quad write by DMA, and re-enters. Two consequences, both documented
in the header rather than left to be discovered:

1. **`data()` must not be dereferenced while a `write()` is in progress.** For
   a single-threaded sketch this is automatic, since `write()` is synchronous.
   It is a real hazard for a read from an interrupt.
2. A write costs a mode flip. Bulk writes should be batched into few large
   `write()` calls rather than many small ones.

`read()` exists alongside the pointer because a caller with a length already
in hand should not have to write the `memcpy`, and because it keeps the API
symmetric. It is a `memcpy` from the mapped window, not a separate path.

## Error handling

`begin()` returns false on: a rate outside what the divider can produce, a
chip that does not identify, or being called twice without `end()`. There is
no error string — there are three causes and a sketch can tell them apart from
what it asked for.

`read()` and `write()` clamp to the device and return the number of bytes
actually transferred, so a caller that runs off the end gets a short count
rather than a wrapped address silently corrupting the bottom of the array.
Both return 0 before `begin()`.

## Testing

**Hardware**, in `tests/hw/test_psram.py` with its own sketch:

1. **The chip identifies.** `0x0D`/`0x5D`, and the EID is stable across
   reboots — it is a per-die serial, so a changing one means a marginal link.
2. **`begin()` refuses when it should**, and `size()` stays 0.
3. **Read-back at both ends of the array**, plus the aliasing check the first
   probe already did: a pattern at the top must not disturb the bottom. This
   is what proves the high address lines decode rather than folding.
4. **A walking-address density sweep** — a unique value at every power-of-two
   boundary, then verify. This is what actually establishes 8 MB; the earlier
   "top and bottom differ" check would also pass on a 4 Mbit part.
5. **The memory-mapped pointer agrees with `read()`** byte for byte. Two paths
   to the same data that could silently diverge.
6. **A 64 KB burst leaves the rest of the array intact** — the tCEM result
   above, kept as a regression test, because it is the assumption `data()`
   rests on and it is temperature-dependent.
7. **Throughput is reported, not asserted.** A number that drifts with clock
   settings should be visible without failing a build.

## Out of scope

- QPI mode (`0x35`). The `0xEB`/`0x38` commands take their instruction on one
  line and address/data on four, so quad speed is already available without
  ever entering QPI — and staying out of it removes a whole class of
  mode-state bugs where the chip and the driver disagree about the protocol.
- Using PSRAM as heap for `malloc()`. A newlib `sbrk` over a region that is
  read-only through its fast path is its own design.
- QSPI1, and any second QSPI device.
- Changing `ch32h4_pin_af()` to write `GPIOx->SPEED`. The gap is real and gets
  documented, but changing it touches every pin the core configures and
  belongs in its own change with its own testing.

## Order of work

1. `ch32h4_qspi` bring-up inside the library: pins, QSPI2 at AF7, indirect
   quad read/write, and the ID check. Landable against a test that asserts
   `0x0D`/`0x5D` and a read-back.
2. Memory-mapped mode, `data()`, and the agreement test against `read()`.
3. DMA for `write()`, plus the mode-flip discipline and its hazards.
4. Examples, the density sweep, the burst regression, and `hazards.md`.
