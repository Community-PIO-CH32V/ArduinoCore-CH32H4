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

---

## The V5F's trap vector table cannot live in flash

**Symptom.** The board boots, prints, and then appears to boot-loop: the V3F
banner repeats endlessly, garbled and interleaved. It starts exactly one
millisecond after the SysTick interrupt is enabled -- about twelve characters
at 115200 baud, which is what made the timing legible.

**What it actually was.** Not a reset loop. GDB on `wch_riscv.cpu.1` showed the
**V5F** halted with `pc = 0x00000000`, which is `_start_v3f`: the second core
had fallen into the *first* core's reset vector and was re-running the whole
V3F boot, wake included. Hence the repetition, and hence the interleaving --
both cores were printing.

`mcause = 0x2` (illegal instruction), `mepc = 0x800800`, an address in no
section of the image.

**Cause.** In `mtvec` mode 3 the trap hardware reads an absolute handler
address out of the vector table at the moment the interrupt fires. That read
does not take the path ordinary instruction fetch takes, and **from flash it
returns garbage**. The core jumps to a nonsense address, takes an illegal
instruction, and traps again into whatever `mtvec` then resolves to.

**Fix.** The vector table is linked into ITCM and copied there by the V3F in
`_load_base_v3f`, alongside `.data` and `.itcm_text`. `mtvec` then reads
`0x200a0003`. Ordinary code still runs XIP from flash -- only the 596-byte
table has to be in RAM.

**How it was found.** Both prior ports place `.vector` inside `.highcode`,
which is `>RAM_CODE AT>FLASH`. Neither says why. Once every other difference
had been eliminated -- the CSRs matched MicroPython's working V5F exactly, the
vector entry pointed at the right handler, and the handler disassembled to a
correct ISR ending in `mret` -- the placement was the only thing left.

## An interrupt attribute on the definition is silently ignored

**Symptom.** None at build time. At run time the first interrupt corrupts the
machine.

**Cause.** `__attribute__((interrupt("WCH-Interrupt-fast")))` written on the
function *definition* is applied after GCC has already emitted the prologue.
You get an ordinary function -- one that ends in `ret` rather than `mret` --
installed in the vector table, with no warning of any kind.

**Fix.** `CH32H4_IRQ_HANDLER()` in `ch32h4_irq.h`. The attribute goes on a
declaration that precedes the definition.

## SysTick: only four CTLR bits, and a per-core acknowledge

`CTLR` takes `EN | IE | NO_RTC | AUTO_RELOAD` -- bits 0 to 3, per reference
manual 4.6.1.1. Setting more (0x3F was tried) is not "more of the same".

There is **one** status register for both cores' timers, in `SysTick0`, and
core N owns bit N of it. The acknowledge is a read-modify-write of that bit,
not a store of zero to the register: a store clobbers the other core's flag.

## micros() must fold in the pending tick, or it runs backwards

Between the counter wrapping and the SysTick ISR actually running, the
millisecond count is one behind what `CNT` implies. A reader that trusts
`s_millis` returns `ms*1000 + 0` immediately after having returned
`ms*1000 + 999`. Retrying while `ms != s_millis` does not help -- `s_millis`
has not changed yet.

`micros()` masks interrupts, reads the overflow flag, and adds the tick the ISR
has not yet counted. Caught by `test_micros_is_monotonic`, which is worth
keeping precisely because the window is small enough to look like it works.

## Mixing OpenOCD and wlink wedges the probe

Confirmed here, as the porting guide warns. After an OpenOCD/GDB session,
`wlink status` returned `WCH-Link underlying protocol error: 0x55` for every
subsequent operation. Only a physical USB replug cleared it.

OpenOCD is still the right tool for *debugging* -- halting cpu.1 and reading
`mcause`/`mepc` is what found the vector-table bug, and nothing else would
have. Just do not flash with it, and expect to replug afterwards.
