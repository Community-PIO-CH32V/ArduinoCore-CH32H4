# The QSPI clock ceiling: what is measured, and what is not explained

**Status: the tidy explanation was wrong.** An earlier version of this document
argued that 25 MHz was a hard round-trip timing limit — that a read has to
complete a loop through the PSRAM inside half a clock period, and that this
budget runs out somewhere between 25 and 33 MHz. That story is refuted by the
board: **reads work at 100 MHz.** The argument is kept below only because
knowing why it is wrong is worth as much as the measurements.

## 1. What was actually measured

Two independent sweeps, on the ground-plane PCB, at every prescaler the
controller offers. Writes and reads are clocked separately — one direction at
the test clock, the other at the default 25 MHz — so the failing side can be
identified.

| divider | SCLK | short reads (1–1024 B, memcpy) | 64 KB streamed read | writes |
|---|---|---|---|---|
| 8 | 12.5 MHz | clean | — | — |
| 6 | 16.7 MHz | clean | — | — |
| 5 | 20.0 MHz | clean | — | — |
| 4 | **25.0 MHz** | **clean** | **0 errors / 16384** | **clean** |
| 3 | 33.3 MHz | **fails** | 0 errors / 16384 | clean |
| 2 | 50.0 MHz | clean | — | **fails** |
| 1 | 100.0 MHz | clean | 2 errors / 16384 | clean |

Throughput, which is also how the clock itself was verified:

| SCLK | 64 KB streamed read | MB/s | vs. 25 MHz |
|---|---|---|---|
| 25.0 MHz | 5250 µs | 12.47 | 1.00× |
| 100.0 MHz | 1331 µs | 49.2 | 3.94× |

The 3.94× confirms the prescaler does what the arithmetic says: a requested
100 MHz really is 100 MHz, and it really does stream at the 4-bit line rate
(49.2 against a theoretical 50.0 MB/s).

## 2. Why the round-trip story is dead

The old argument was that a read must satisfy

```
t_pad_out + t_flight + t_ACC + t_flight + t_pad_in + t_setup  ≤  T/2
```

and that this fails between `T/2` = 20 ns (25 MHz) and 15 ns (33.3 MHz).

**A setup-time violation is monotonic in frequency.** It cannot start
failing at 33.3 MHz and then start working again at 50 and 100 MHz, because
the budget only ever shrinks. The measured pattern is non-monotonic in both
directions — reads fail only at divider 3, writes fail only at divider 2 — so
whatever is happening, it is not a delay chain running out of room.

That also disposes of the reasoning by which the old document dismissed the
circuit board. The conclusion (the board is not the problem) still holds, but
now for a better reason than an arithmetic one: see section 4.

## 3. What the failures actually look like

Neither failure resembles analog marginality.

**Reads at 33.3 MHz corrupt exactly 32 bytes**, regardless of transfer length:

| transfer length | 64 | 128 | 256 | 1024 |
|---|---|---|---|---|
| bad bytes per repetition | 32 | 32 | 32 | 32 |

A per-bit sampling failure would scale with length. A fixed 32-byte block does
not — and 32 bytes is the QSPI FIFO depth. Further, the *same clock* is clean
when the same 64 KB is read as a stream of aligned words (0 errors in 16384)
and dirty when read through `memcpy` into short buffers. So at divider 3 the
corruption depends on **access width and pattern**, not on the clock rate.

**Writes at 50 MHz corrupt exactly half the bytes:**

| length | 2 | 4 | 8 | 16 | 32 | 64 | 128 | 256 | 1024 |
|---|---|---|---|---|---|---|---|---|---|
| bad bytes per repetition | 1 | 2 | 4 | 8 | ~16 | 32 | ~64 | ~128 | ~510 |

Exactly `n/2`, every time, deterministically. Every other byte. That is a byte
lane or FIFO-feed misalignment — a digital off-by-one in the data path, not a
signal that arrived a nanosecond late.

Both are **deterministic and reproducible**, not intermittent. Analog
marginality produces error rates that drift with temperature and vary run to
run. These produce the same byte counts every repetition.

## 4. So was it ever the wiring?

No — and this is now better supported than it was under the timing argument.

The MicroPython port attributed its low ceiling to breadboard jumper wires,
and this board has since moved to a PCB with a ground plane. If parasitics
were the cause, the ceiling should have moved. Three things say they were not:

- **The failures are divider-specific, not frequency-monotonic.** Wire
  inductance and reflections get worse with faster edges and higher rates,
  without exception. Nothing about wiring makes 33.3 MHz fail while 50 and
  100 MHz pass.
- **The failures are direction-specific.** Reads fail at one divider, writes
  at a different one. Shared traces carry both.
- **The corruption has integer structure** — exactly 32 bytes, exactly `n/2`
  bytes. Analog problems do not produce round numbers.

Flight time is negligible regardless: FR4 microstrip propagates at roughly
6.7 ps/mm, so even a generous 60 mm round trip is ~0.4 ns against a 20 ns
half-period. But the structural evidence above is the stronger argument,
because it does not depend on a delay budget that turned out to be wrong.

**The PCB was worth building and it is not what is holding the clock down.**

## 5. What is not explained

The mechanism. Candidates, none established:

- **Duty-cycle asymmetry at odd dividers.** An integer divider from HCLK
  cannot produce 50% duty on an odd divisor: divider 3 is 2 HCLK high and
  1 HCLK low, so one phase is only 10 ns. This neatly explains divider 3
  failing — but it predicts divider 5 (also odd, 20/30 ns) should be marginal
  and it is clean, and says nothing about divider 2 failing on writes. It is
  suggestive, not sufficient.
- **A clock-domain crossing that is marginal at small dividers.** At divider 1
  SCLK is HCLK and there is no crossing at all; at large dividers there is
  slack. Dividers 2 and 3 are where a synchroniser would have the least
  margin. This fits the shape of the data but is unfalsified guesswork.
- **Access-width handling in the memory-mapped AHB path.** The strongest
  single clue is that divider 3 is clean for aligned word streaming and dirty
  for `memcpy`, at the same clock.

What would settle it: drive a known pattern and capture SCLK and IO0–IO3 on a
scope or logic analyser at dividers 2, 3 and 4 — measuring the actual duty
cycle at the PSRAM pin, and whether the corrupted bytes are wrong on the wire
or only after the FIFO. That is an instrument question, not a firmware one;
nothing further can be resolved from inside the MCU.

## 6. What the library does, and why

**`DEFAULT_CLOCK` stays at 25 MHz** — divider 4, the only setting measured
clean on every test: short and long reads, writes, all ten transfer lengths,
the 64 KB streamed burst, and the tCEM witness rows.

100 MHz is genuinely tempting: it works for writes, for short reads, and
streams a 64 KB read at 49.2 MB/s, four times faster. It is not the default
because it produced 2 corrupt words in 16384 on that burst. A 1.2 × 10⁻⁴ word
error rate is far too high for a memory API where reads are plain pointer
dereferences and nothing checks them — but it is close enough to working that
it is worth revisiting with an instrument, rather than being written off.

**Do not raise the clock on the strength of one passing test.** The evidence
above is precisely a set of configurations that pass some tests and fail
others: divider 3 passes streamed reads and fails `memcpy`; divider 2 passes
reads and fails writes; divider 1 passes everything except a long burst. Any
future change needs the whole matrix — both directions, several transfer
lengths, and a long streamed burst — not a single sketch that appears to work.

## Related

- `docs/hazards.md` — the SIOO, `QSPI_DeInit`, and memory-mapped-hang findings
- `docs/superpowers/specs/2026-09-06-psram-design.md` — the design spec
- `libraries/PSRAM/src/PSRAM.h` — the user-facing version of this warning
