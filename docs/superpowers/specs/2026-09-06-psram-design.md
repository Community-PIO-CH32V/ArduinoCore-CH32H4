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

**The clock ceiling is HCLK, not the core clock.** QSPI2 divides HCLK at
100 MHz; the V5F's 400 MHz never reaches this peripheral, so prescaler 0 is
100 MHz and that is the hardware maximum before anything else is considered.

**25 MHz is a round-trip limit, not a board limit.** Reads must survive
controller pad delay, flight to the chip, the chip's data-valid delay, flight
back, and controller setup — and without a sample shift the controller samples
about half a clock after launching the edge. That budget is 20 ns at 25 MHz
and 15 ns at 33 MHz, and the path fits the first and not the second.

Three measurements say timing rather than signal quality:

- **Quad writes pass at every clock.** A write has no round trip: the
  controller drives clock and data together and the chip samples with its own
  setup and hold. Only reads have to come back.
- **Extra dummy cycles rescue nothing.** Dummy cycles move when the burst
  starts; they do not change each bit's phase against the clock, and the
  failure is per-bit sampling. Simple late-data would have been fixed by them.
- **Slew rate changed nothing**, where a marginal edge would have moved.

STM32's QUADSPI absorbs exactly this with `SSHIFT` (`CR` bit 4). On this part
that bit position is undefined in WCH's header and setting it changes nothing
measurable. `CKMode` (Mode 0 against Mode 3) is the one remaining knob and was
NOT tested during the probe; task 1 tests it before the 25 MHz default is
treated as final.

Separately, single-line reads fail above 33 MHz because `0x02`/`0x03` are the
APS6404L's own slow commands — also not the board.

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

- QPI mode (`0x35`) **for the first release, but not dismissed** — the earlier
  claim that it "buys nothing" was true only for streaming. QPI shortens the
  instruction phase from 8 clocks on one line to 2 on four, and nothing else:
  `0xEB` already carries address and data 4 bits wide. A transaction costs
  `20 + 2N` clocks in SPI mode against `14 + 2N` in QPI, so for a long
  streaming read the saving rounds to zero — which the 12.50 MB/s measurement,
  already 100% of line rate, confirms directly. For a 4-byte random read it is
  28 clocks against 22, about 21% — and random access is half of what this is
  for, so the saving is real.

  It stays out of release one because entering QPI adds a mode-state the chip
  and driver can disagree about, and because `SIOOMode` may capture the same
  saving without that risk: it suppresses the instruction phase on repeated
  transactions. Whether the APS6404L tolerates instruction-less continuation —
  NOR flash does it with mode bits this part may lack — is a measurement, and
  task 2 makes it. If SIOO works, QPI is unnecessary; if it does not, QPI
  becomes a justified follow-up rather than a guess.
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
