# Why QSPI reads stop at 25 MHz, Mode 0

The PSRAM library clocks QSPI2 at 25 MHz off a 100 MHz HCLK, and refuses to go
faster without a measurement. This is the reasoning behind that number.

The short version: **a QSPI read has to complete a round trip inside half a
clock period, and on this part that round trip is between 15 and 20 ns.** At
25 MHz half a period is 20 ns and it fits. At 33.3 MHz it is 15 ns and it does
not. Almost none of that 15-20 ns is the circuit board.

---

## 1. Reads are round trips; writes are not

This asymmetry is the whole story, so it is worth being precise about it.

On a **write**, the controller drives SCLK and the four IO lines from the same
internal clock edge. The data and the clock leave the chip together, travel
together, and arrive at the PSRAM together. Whatever delay the pads and the
traces add, they add it to *both*. The PSRAM samples with the clock it
actually received, against its own setup and hold window. Increasing the clock
frequency does not consume any margin the controller owns — the budget belongs
to the PSRAM, and the APS6404L is a 133 MHz part.

On a **read**, the loop is open. The sequence is:

1. The controller's internal clock edge fires.
2. That edge propagates out through the SCLK pad and down the trace.
3. It arrives at the PSRAM, which uses it to launch a nibble onto IO0-IO3.
4. Those four bits travel back down the traces.
5. They arrive at the controller's input pads and propagate to the sampling
   flip-flop.
6. The flip-flop samples them — using the controller's **internal** clock,
   which is the un-delayed original from step 1.

Step 6 is the trap. The controller samples against a clock that never left the
die, while the data it is sampling has made the entire round trip. Nothing
cancels. Every picosecond spent in steps 2 through 5 is charged against the
budget.

This is why the two directions have completely different ceilings, and why
"writes still work at 50 MHz" is not evidence that reads should.

## 2. The budget is half a clock period

In SPI Mode 0 the device launches read data on one SCLK edge and the
controller samples it on the next — half a period later. (This is the STM32
QUADSPI convention that this controller clones; ST document it as "the QUADSPI
samples data 1/2 of a CLK cycle after the data is driven by the external
device".) So the constraint is:

```
t_pad_out + t_flight_clk + t_ACC + t_flight_data + t_pad_in + t_setup  ≤  T/2
```

where `T = 1 / f_SCLK`, and the terms are:

| term | what it is |
|---|---|
| `t_pad_out` | controller SCLK pad: internal edge to the pin |
| `t_flight_clk` | PCB, MCU SCLK pin to PSRAM SCLK pin |
| `t_ACC` | PSRAM access time: clock edge in, valid data on IO |
| `t_flight_data` | PCB, PSRAM IO pins back to MCU pins |
| `t_pad_in` | MCU input pad and synchroniser to the sampling flop |
| `t_setup` | setup time at that flop |

Half-periods:

| f_SCLK | T | T/2 |
|---|---|---|
| 25.0 MHz | 40.0 ns | **20.0 ns** |
| 33.3 MHz | 30.0 ns | **15.0 ns** |
| 50.0 MHz | 20.0 ns | 10.0 ns |
| 100.0 MHz | 10.0 ns | 5.0 ns |

## 3. What the measurements bracket

Rather than predict the ceiling from datasheet numbers, the measurements bound
the left-hand side directly. 64-byte quad reads pass at 25 MHz and fail at
33.3 MHz, so:

```
15 ns  <  (the whole delay chain)  ≤  20 ns
```

That is a tight bracket, and it is enough to rule out the usual suspects.

**The circuit board is not in the running.** Signals on FR4 microstrip travel
at roughly 6.7 ps/mm. A generous 60 mm round trip — 30 mm out, 30 mm back on a
breakout — is about **0.4 ns**, which is 2% of a 20 ns budget. Even a
pathological 200 mm round trip would be 1.3 ns. Nothing about trace length,
ground plane quality, or decoupling can move a 15-20 ns delay chain by the
5 ns that separates passing from failing. This is the quantitative form of the
conclusion reached on the bench: the improved PCB was never going to lift this
ceiling, because the board was never spending the budget.

**The PSRAM is not the binding constraint either.** The APS6404L is specified
to 133 MHz, where a full period is 7.5 ns. Its access time therefore has to be
a small number of nanoseconds; a part with a 15 ns access time could not be a
133 MHz part in any mode. Call it under 10 ns and it still leaves most of the
bracket unaccounted for.

**So the budget is spent inside the MCU** — in `t_pad_out`, `t_pad_in`, and the
input synchroniser. That is unsurprising for a general-purpose IO pad on a
part whose QSPI has no calibration hardware, and it is the term nobody outside
WCH can change.

## 4. Why there is no knob for it

Controllers that go faster than this solve it by moving the sampling point
rather than by making the round trip shorter:

- **Sample shift.** ST's QUADSPI has `SSHIFT` (CR bit 4), which delays sampling
  by a further half cycle, turning the budget from `T/2` into `T`. On this part
  that bit position is undefined in WCH's header, and setting it changed
  nothing measurable.
- **DQS / read strobe.** Higher-end memory controllers have the device return a
  strobe alongside the data, making the read source-synchronous — the round
  trip then cancels the way it does for writes. QSPI has no such line.
- **Delay-line calibration.** Not present here.

Without one of those, `T/2` is a hard architectural limit, not a tuning
problem.

## 5. Why Mode 3 is worse, not better

`CKMode` was the last plausible knob, on the theory that the other clock
polarity might land the sampling edge somewhere kinder. It does not. Measured:

| CKMode | Clock | 1-byte read (content verified) | 64-byte read (content verified) |
|---|---|---|---|
| Mode 0 | 25.0 MHz | pass | **pass** |
| Mode 0 | 33.3 MHz | pass | fail |
| Mode 3 | 25.0 MHz | pass | fail |

In theory Modes 0 and 3 are interchangeable mid-transfer — that is the usual
claim, and it is why they are the two "compatible" SPI modes. They differ in
the idle level of SCLK and therefore in *which physical edge* does the
launching and which does the sampling: in Mode 0 the sampling edge is a rising
edge, in Mode 3 a falling one.

They are only equivalent if the clock's duty cycle is exactly 50% all the way
to the PSRAM's clock pin. It is not exactly 50%: the SCLK output pad has
different propagation delays for a rising and a falling edge, and a capacitive
load with asymmetric drive strength skews it further. If the pad is, say, 2 ns
slower to rise than to fall, then one mode's effective half-period is 2 ns
longer than nominal and the other's is 2 ns shorter. Against a 20 ns budget
with only ~2-5 ns of slack, that is the difference between passing and
failing — which is exactly the size of effect observed.

So Mode 0 is not an arbitrary default. It is the polarity whose sampling edge
lands on the longer half of a slightly asymmetric clock.

## 6. An honest loose end: the length dependence

The failures above are **length-dependent**, and this is not fully explained.

In both failing configurations, a 1-byte read returned correct data while a
64-byte read did not. A pure static-timing violation should corrupt the first
nibble as readily as the hundredth, so something is accumulating.

Candidate explanations, none of them established:

- **Marginal sampling is probabilistic, not deterministic.** If each nibble has
  a small independent chance of being sampled wrong, a 2-nibble transfer
  usually survives and a 128-nibble one usually does not. This fits the data
  but predicts occasional 1-byte failures, which were not looked for.
- **The first byte is not sampled under the same conditions.** The nibble
  immediately after the dummy cycles follows a bus turnaround, and the
  controller may treat it differently from nibbles arriving mid-stream.
- **Something in the streaming path degrades with burst length** — FIFO
  pipelining, or CE# being held low long enough to matter.

What would settle it: a sweep that verifies *content* at 1, 2, 4, 8, 16, 32,
64, 128, 256 and 1024 bytes, repeated enough times per length to estimate a
per-nibble error rate, with reads and writes clocked separately so the two
paths can be told apart. An attempt at this hung the board — it re-initialised
the controller roughly eighty times in quick succession and tripped the
memory-mapped hazard described in `hazards.md` — so it needs to be built to
re-initialise once per clock setting, not once per repetition.

Until then, treat the length dependence as observed and unexplained. It does
not affect the conclusion about the ceiling, because the library's transfers
are long ones and those are the case that fails first.

## 7. What this means in practice

- **25 MHz stands**, and is confirmed against both clock modes.
- **Reads are already at line rate.** Memory-mapped reads measure 12.47 MB/s,
  against a theoretical 25 MHz x 4 lines = 12.5 MB/s. There is no controller
  overhead left to optimise away; the only way to go faster is a faster clock,
  which is what this document is about.
- **QPI mode would not help.** It removes the instruction phase, not the round
  trip. See the design spec: worth ~21% on a small random read, ~0% on
  streaming, and nothing at all on the ceiling.
- **Writes could safely run faster than reads**, since they have no round trip.
  The driver deliberately uses one clock for both. Splitting them would mean
  re-initialising the controller around every write, and the mode flip would
  cost more than the extra megahertz returns.

## Related

- `docs/hazards.md` — the SIOO, `QSPI_DeInit`, and memory-mapped-hang findings
- `docs/superpowers/specs/2026-09-06-psram-design.md` — the design spec
- `libraries/PSRAM/src/PSRAM.h` — the user-facing version of this warning
