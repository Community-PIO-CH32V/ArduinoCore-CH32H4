# Silent failures this core has actually hit

The porting guides for this silicon open with the observation that nearly
everything expensive fails without producing an error. This file records the
ones **this port** hit, with the evidence that found them, so the next person
does not spend the afternoon again.

Failures inherited from the prior ports are in the design spec, section 2.5.
These are new.

---

## `mstatus = 0x6088` drops the core into User mode, and the board goes silent

**Symptom.** After flashing, the board emits nothing at all on USART1. Not
garbage, not a partial line -- zero bytes. The probe attaches, `wlink status`
identifies the chip correctly, `wlink flash` reports `Flash done` with no
`0x55`, and `wlink reset` succeeds. Indistinguishable from a dead chip.

**Cause.** WCH's own `startup_ch32h417_v3f.S` writes `0x6088` to `mstatus`
before its `mret`. That leaves **MPP = 00**, so the `mret` returns into *User*
mode, where every M-mode CSR is unreachable. The first `__disable_irq()` or
equivalent takes an illegal-instruction trap (`mcause = 2`) into a fault
handler that cannot print yet, and the board stops.

**Fix.** `0x7888` = FS=11 (FPU on) | **MPP=11 (stay in Machine mode)** | MPIE |
MIE. See `cores/ch32h4/startup_v3f.S`.

**How it was found.** By flashing a known-good control image -- the libhal
port's `firmware.bin`, which uses the same USART1 on the same pins at the same
baud -- and watching it print correctly. That proved the probe, the flash path,
the serial path and the board were all fine in about a minute, and moved the
fault unambiguously into our firmware. Reading that port's startup then found
the deviation, with its reasoning already written in a comment.

**The lesson generalises:** keep a known-good image to hand and flash it the
moment a result surprises you. "Verify the tool before blaming the firmware."

---

## The RCC clock tables live in `.data`, so `.data` must be copied before any clock call

**Symptom.** Same total silence, for a different reason.

**Cause.** `RCC_GetClocksFreq()` -- which `USART_Init()` calls to compute the
baud divider -- indexes six lookup tables declared

```c
static __I uint8_t PLLMULTable[32] = { ... };
```

`__I` is `const volatile`, and the `volatile` stops GCC placing them in
`.rodata`. They land in **`.data`**, at `0x200C0100`. A startup that does not
copy `.data` from flash leaves them holding whatever DTCM contained, so every
computed frequency is wrong and `USART_Init` programs a baud rate nothing can
read.

**Fix.** The V3F copies `.data`, zeroes `.bss` and copies `.itcm_text` in
`_load_base_v3f`, which runs from RAM.

**Consequence for the single-ELF design.** Both cores share one `.data` and one
`.bss`, so **whichever core runs first owns the runtime data init and the other
must not repeat it**. A second `.bss` zero on the V5F would wipe the clock
state the V3F had just established. `startup_v5f.S` therefore copies nothing;
it sets up its own stack, CSRs and vector table and runs the static
constructors that belong to the sketch.

---

## `--print-memory-usage` reports `0 GB` for an empty region

Cosmetic, but worth knowing before it wastes a minute: GNU ld 2.38 prints
`0 GB` rather than `0 B` for a region with nothing in it. The regions are fine.

---

## A lost banner reads exactly like a baud-rate bug

**Symptom.** The tail of the banner arrives perfectly but the first ~50
characters are missing and what remains starts mid-byte:

```
'"k���100000000\r\nsysclk=400000000\r\nboot ok'
```

**Cause.** Not the firmware. `wlink flash` resets the part when it finishes, so
the board prints its whole banner before the test harness opens the serial
port. What the harness then catches is whatever was still in flight.

**Fix.** Open the port first, then reset deliberately. `tests/hw/conftest.py`
does this in `reset()`, called after the port is open.
