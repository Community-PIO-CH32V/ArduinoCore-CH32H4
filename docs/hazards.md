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

## The V5F's instruction cache is OFF at reset, and it is worth 145x

**Symptom.** Everything works. It is just extremely slow, and only if you
measure it against something do you find out.

**Measured**, same loop compiled twice, one copy in flash and one in ITCM:

| | XIP (flash) | ITCM | ratio |
|---|---|---|---|
| I-cache off (the reset default) | 365,606 us | 2,517 us | **145x** |
| I-cache on | 2,507 us | 2,505 us | **1.00x** |

**Cause.** QingKeV5 manual, `cache_strtg_ctlr` (CSR `0xBC2`), bit 1
`ic_disable`: *"Instruction cache disable flag bit, which is 0 to turn on the
instruction cache function."* **Reset value: 1.** The 32 KB instruction cache
is disabled out of reset and nothing anywhere says so.

The per-region enables above it -- bit 24 `ic_code_strtg` for
`0x00000000-0x1fffffff`, bit 25 `ic_sram_strtg` for `0x20000000-0x3fffffff` --
all reset to **1**, so clearing the single master disable bit is the whole fix.
Flash is clocked at HCLK/2 = 50 MHz while this core runs at 400 MHz, and
uncached, every instruction fetch pays that.

**Fix.** WCH's own example, `EVT/EXAM/CPU/ICache`, has the sequence, and the
load-bearing detail is not something anyone would guess. The final instruction
is a **`csrc`**, and it clears **bits 24 and 25 as well as bit 1**:

```asm
li t0, 0x03000002
csrc 0xbc2, t0
```

Bits 24 and 25 are `ic_code_strtg` and `ic_sram_strtg` -- the blanket "cache
everything in `0x00000000-0x1fffffff` / `0x20000000-0x3fffffff`" enables -- and
they **reset to 1**. Clearing only `ic_disable`, which is the obvious reading of
the manual, leaves them on: the cache then tries to cover all of flash *and* the
TCM/SRAM window, including the ITCM the vector table and hot code live in, and
the core traps in startup.

WCH scopes caching with a **PMP entry** instead. Full sequence, in
`startup_v5f.S`:

1. `pmpaddr0` = `_cache_beg >> 2`, `pmpaddr1` = `_cache_end >> 2` (TOR).
2. `cache_pmp_ovr` (CSR `0xBC3`) = `0x10` -- bit 4 `ic_pmp1cache_strtg`, so
   instructions matched by PMP channel 1 may be cached. The manual says this
   overrides the region policy in `0xBC2` for addresses the channel covers.
3. `pmpcfg0` (CSR `0x3A0`) = `0xAD00` -- channel 1 locked, TOR, read+execute.
4. `opcache_ctlr` (CSR `0xBD0`) = `0x4` -- flush.
5. `csrc 0xBC2, 0x03000002` -- enable, and turn the blanket regions off.

The linker script brackets `.text` and `.rodata` with `_cache_beg` /
`_cache_end`, so the window is the flash-resident code. ITCM is deliberately
left outside it: it is already zero-wait.

Result: **6/6 clean boots and flash at 1.00x of ITCM.**

Four attempts failed before the example was consulted -- clearing `ic_disable`
alone, invalidating index 0 first, adding `fence.i`, and invalidating all 1024
lines. All of them left bits 24 and 25 set, which was the actual problem in
every case. The lesson is the cheap one: look for the vendor example first.

**Why nobody found it before.** The MicroPython port avoided the question by
copying 392 KB of `.text` into RAM and running from there; the libhal port ran
on the V3F, which has no instruction cache at all. Neither had reason to look,
and both paid for it -- MicroPython in RAM it could have given to its heap.

This is what makes the XIP-primary memory strategy work: code runs from flash
at ITCM speed, and the ~700 KB of RAM stays available to sketches.

**A dead end worth recording**, since it looks plausible: `FLASH_ACTLR`'s
`EHMOD` "enhance mode" bit, which the SDK exposes as `FLASH_Enhance_Mode()`,
makes no measurable difference here (365,360 us against 365,606 us). It is not
the code accelerator it resembles on other CH32 parts.

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

**Recovering a wedged probe.** Nothing over the wire works: `wlink reset dm`,
`reset halt` and `reset run` all return `0x55` themselves, because the latch is
in the probe's USB protocol layer, below anything wlink can ask it to do. So
there is no automating this.

What does work, in increasing order of annoyance:

1. **Hold the board's NRST down while flashing.** The WCH-Link has NRST fitted,
   so this is a wire that is already there. This is the cheapest recovery and
   it is what got the bench back the last two times.
2. Unplug the WCH-Link's own USB from the host, wait, plug it back.
3. Both at once -- probe USB out *and* board power out -- then probe first.

Power-cycling the board **alone** does not clear it, which is worth knowing
before spending a trip to the bench: the latch is in the probe.

Expect this every few dozen flash cycles. `tests/hw/conftest.py` reports it as
a TOOL FAILURE with its own exit status precisely so it never gets mistaken
for a firmware regression -- which it looks exactly like.

---

## Exceptions: what it takes to make a throw work under -nostartfiles

Working, 5/5 on hardware. Two things are needed and neither produces a build
error when missing.

**1. `.eh_frame` needs its terminating zero word.** The registry-based unwinder
walks CIE/FDE records until it reads a zero length, and crtend.o normally
supplies that as `__FRAME_END__`. Under `-nostartfiles` crtend is never linked,
so the linker script appends `LONG(0)` at the end of the section. Without it the
unwinder runs off the end and dereferences whatever follows.

**2. Nothing registers `.eh_frame` at all.** This libgcc uses the
registry-based FDE lookup and crtbegin's `frame_dummy` never runs, so
`__register_frame_info(__eh_frame_start, ...)` is called from a
priority-101 constructor in `ch32h4_eh.cpp`.

While the instruction cache was misconfigured, a `throw` faulted with
`mcause=5` (load access fault) inside `strlen`. Both of the above were already
in place at the time; the throw only started working once the cache was scoped
correctly. The two are not obviously related and the exact interaction was not
chased further, since the correct cache configuration is required anyway.

`__gnu_cxx::__verbose_terminate_handler` is also overridden, which keeps the
43 KB C++ name demangler out of the image -- libstdc++'s documented
customisation point.

---

## `IC_Str` is pmpcfg bit 5, not the bit 6 the manual gives

**Symptom.** None. The board boots, enumerates USB, gets a DHCP lease, passes
every functional test, and runs **143x slower** than it should.

**Cause.** The instruction cache is scoped by a PMP entry: `IC_Str` in
`pmp<i>cfg` is what makes the `cache_pmp_ovr` policy apply to that region.
Table 4-3 of the QingKeV5 Microprocessor Manual puts `IC_Str` at **bit 6** and
marks bit 5 reserved. WCH's own `EVT/EXAM/CPU/ICache` writes `0xAD00`, which
sets **bit 5** and leaves bit 6 clear. The manual and the vendor's example
disagree, and the silicon follows the example.

Measured, with the same loop compiled into flash and into ITCM:

| `pmpcfg0` | flash | ITCM | ratio |
|---|---|---|---|
| `0xAD00` (bit 5) | 140,491 cyc | 160,908 cyc | **0.87x** |
| `0xCD00` (bit 6) | 20,095,779 cyc | 166,976 cyc | **143x** |

**Why it is dangerous.** Nothing reads back differently. `pmpcfg0`,
`cache_pmp_ovr` and `cache_strtg_ctlr` all return exactly what was written in
both cases, so no amount of register inspection distinguishes a cache that is
covering the region from one that is not. Only a cycle count does.

**How it was found.** By benchmarking, after a manual-driven "correction" from
bit 5 to bit 6 was made and nothing failed.

**Fix.** `PMPCFG_IC_STR = (1 << 5)` in `cores/ch32h4/ch32h4_csr.h`, and
`test_instruction_cache_is_on` in `tests/hw/test_acceptance.py` now *gates* on
the ratio rather than merely recording it.

---

## `hw_popdm_addr` resets to the DTCM base, and 512 bytes there are not yours

**Symptom.** None, until something else is put at `0x200C0000`.

**Cause.** With `HWSTKEN` set in `intsyscr` -- WCH's own startup sets it, and so
does this core -- the V5F saves the caller-saved registers to *memory* on every
trap entry, at the address in `hw_popdm_addr` (CSR `0xBC4`). The manual gives
its reset value as `0x200A0000`. **This silicon reports `0x200C0000`**, which
is the DTCM base, and is consistent with the register's own description
("points to the DTCM area") and with WCH's V5F linker script, which starts its
first region at `0x200C0000 + 512` and never says why.

This port's linker script had `.loadcode` at `0x200C0000`. Dumping that address
before and after the first interrupts showed saved registers landing on top of
it -- a return address, and a live USART1 base among them. `.load` only runs
once, during the V3F's startup, so it survived; anything else there would not
have.

**Fix.** A reserved `HW_STACK` region of 512 bytes at the DTCM base, and
`hw_popdm_addr` is left at its reset value rather than written.

**The lesson.** The manual's reset values for this family are not reliable.
Read the register back before building on the documented number -- it costs one
`csrr` and one print.

---

## The V3F's vector table was in flash, and 60 of its entries were zero

**Symptom.** Single-core sketches are fine indefinitely. The moment a sketch
defines `setup1()`/`loop1()`, the board lockup-resets about 25 times a second
with no fault record at all -- `rst=0x80000000 lockup` and nothing else.

**Cause.** Two problems, both dormant for as long as the V3F never took a trap.

1. `.v3f_vector` was linked into `FLASH_V3F`. In `mtvec` mode 3 the trap
   hardware reads an absolute address out of the table, and that read from
   flash returns garbage -- the same hazard already documented above for the
   V5F, and fixed there but not here. WCH's own V3F linker script puts
   `.vector` in RAM.
2. The 60 unused entries were `.word 0`. A zero entry in mode 3 means "jump to
   address 0", and address 0 on this part is `_start_v3f`, so a stray interrupt
   silently re-ran startup. On the console that is indistinguishable from a
   watchdog reboot.

The V3F got away with both because in a single-core sketch it wakes the V5F and
sleeps, and never traps.

**Fix.** `.v3f_vector` is linked into a `V3F_VECTOR` region in shared SRAM and
copied there by `_load_base_v3f`, alongside the V5F's table. Every unused entry
points at `Stray_IRQ_v3f`, which records `mcause`/`mepc`/`mtval`/`sp`/`ra` into
`.xcore` and resets, so the next boot prints what happened.

---

## `yield()` and the USB interrupt both ran `tud_task()`, with only one locking

**Symptom.** A hang inside an interrupt handler, minutes to hours in, under
combined USB and network load.

**Cause.** `ch32h4_usb.c` runs `tud_task()` from the USBFS interrupt as well as
from `yield()` -- it has to, or a sketch that blocks in `loop()` lets TinyUSB's
event FIFO fill, and a full FIFO is a `TU_ASSERT` that kills the stack. The
interrupt took an `s_in_task` guard. `yield()` called Adafruit's
`TinyUSB_Device_Task()`, which is a one-line wrapper around `tud_task()` and
takes nothing, and `TinyUSB_Device_FlushCDC()`, likewise. So the interrupt
could land inside a `tud_task()` started by `yield()`, see the guard clear, and
re-enter the event queue on top of it.

**Fix.** `ch32h4_usb_lock()` / `ch32h4_usb_unlock()`, a test-and-set with
interrupts masked, exported from `ch32h4_usb.c`. `yield()` holds it across both
Adafruit calls; the interrupt takes it and gives up if it is held.

---

## A fault handler that spins wedges the probe; one that resets can lock you out too

**Symptom.** After a firmware fault, `wlink` returns
`underlying protocol error: 0x55` for every operation, including `erase`. The
only way back is holding NRST down through a flash or erase.

**Cause.** A core spinning with interrupts disabled stops answering the debug
module. But simply resetting instead is not enough on its own: a fault that
reproduces on every boot resets several times a second, and the probe never
gets a window in which to attach.

**Fix.** Both halves. The fault handler records to `.xcore` and resets rather
than spinning -- and `ch32h4_fault_log.boot_faults` counts consecutive
crash-reboots, so after `CH32H4_FAULT_REBOOT_LIMIT` the V3F prints the record
and *stops waking the V5F*. The board stays talkable and reflashable. The V5F
clears the count once it reaches runtime-ready.

---

## `.xcore` survives a reflash, so a magic word is not enough to trust it

**Symptom.** A boot report claiming `boot_faults=628260155`, and interrupt
counters in the billions, on a board that had just been flashed.

**Cause.** `.xcore` is `NOLOAD`, which is the point -- it is how a fault record
survives the reset that follows it. But it also survives a *reflash*, so a new
build whose variables sit at different offsets reads the previous image's bytes
through the new layout, and finds the validity magic exactly where it expects
it.

**Fix.** Two things, because neither is sufficient alone. The magic has the
record's `sizeof` folded into it, so a changed struct invalidates the region.
And everything read out of `.xcore` is range-checked against what it can
legitimately be, because adding a *variable* changes offsets without changing
any `sizeof`.

---

## USART1 has two drivers and two cores, and nothing serialised them

**Symptom.** Console output interleaved mid-word: `V35F: alive core_id=1`,
`F: waitin5F: usb up`. Worse than cosmetic -- it destroys the post-mortem at
exactly the moment it matters, and makes a reproducible fault look like a
different one on every boot.

**Cause.** `ch32h4_console_*` (raw, polling) and `HardwareSerial` both poll
`TXE` and store to `DATAR` on USART1, and both cores use both. Nothing in the
peripheral serialises that.

**Fix.** `CH32H4_HSEM_CONSOLE`, a hardware semaphore taken by every writer.
Recursive per core -- HSEM records the taking core as owner and refuses a
second take from it, so the depth is counted in software, in `.xcore` because
`.bss` is shared. `HardwareSerial::write(const uint8_t*, size_t)` is overridden
rather than inherited from `Print`'s byte loop, so a whole `print()` goes out
under one take. The spin is bounded: a core that dies holding the semaphore
must not take the console with it.

---

## A call to address 0 links cleanly, and address 0 is the other core's reset vector

**Symptom.** A sketch boots, prints its banner, reaches its prompt, and then the
board lockup-resets several times a second. No fault record. No `=== TRAP ===`.
The console shows a complete, clean boot every time, so it reads as a reset
loop with no cause -- and it only happens with *some* sketches.

**Cause.** `static String line;` inside a sketch's `loop()`. Any function-local
static with a non-trivial destructor is enough. GCC constructs it under a guard
and then emits a call to `__cxa_atexit` to register the destructor. Under
`-nostartfiles` nothing provided `__cxa_atexit`, and the reference resolved to
**zero**:

```
b150:  sb    a5,-1968(gp)     # set the guard
b154:  auipc ra,0x0
b158:  jalr  zero             # 0 <_start_v3f>
```

Address 0 on this part is `_start_v3f`. So the V5F did not fault -- it jumped
into the *V3F's reset vector* and re-ran the other core's startup, which resets
`sp` to `_estack_v3f`, re-copies `.data` and re-zeroes `.bss` underneath a
running system. That corrupts the V3F's stack, so the V3F then returns through
a garbage `ra` and takes an instruction access fault of its own. The V3F's
symptoms are downstream of the V5F's jump, which is why this looked like a
dual-core bug for a long time. It has nothing to do with the second core.

The linker said nothing: no undefined symbol, no warning, and no entry in
`nm -u`.

**Fix.** `cores/ch32h4/ch32h4_cxx.cpp` defines `__cxa_atexit`, `atexit`,
`__dso_handle`, `__cxa_pure_virtual` and `__cxa_deleted_virtual`. Doing nothing
and returning success is correct for the first two: those destructors run at
`exit()`, and a sketch never exits.

**The guard.** `test_nothing_calls_address_zero` in `tests/test_link_matrix.py`
disassembles every sketch and fails on any call whose resolved target is 0. It
generalises to the next such symbol, whatever that turns out to be -- and there
will be one.

Two functions are allowlisted, because they use the weak-symbol idiom on
purpose: `yield()` (`ch32h4_ticker_update`, `ch32h4_net_update`) and
`ch32h4_v3f_main()` (`setup1`, `loop1`). An undefined weak symbol *is* address
zero, so the call is emitted with a zero target and the `if` in front of it
makes the instruction unreachable. Keeping that list short and explicit is the
point: a call to zero anywhere else is one nothing null-checks.

---

## `HSEM_FastTake()` returns success exactly when it did nothing

**Symptom.** `CH32H4.mutexTryLock()` inverted:

```
mutex_first=0            # should be 1 -- it was free
mutex_second=1           # should be 0 -- this core already held it
mutex_after_unlock=0     # should be 1
```

**Cause.** Reading `HSEM->RLRX[n]` *is* the take -- one bus read locks the
semaphore if it was free and records the owner. The read returns the state
**before** the take, so zero is what success looks like. Measured on the V5F
with the semaphore free:

```
RLRX read 1 -> 0x00000000   (was free: this read acquired it)
RLRX read 2 -> 0x80000100   (already ours)
RX          -> 0x80000100
release
RLRX read 3 -> 0x00000000   (acquired again)
```

The SDK's helper compares that value against `(coreid << 8) | (1 << 31)` and
calls a match `READY`. So it reports success exactly when the take was a
**no-op because this core already held the semaphore**, and reports failure on
the read that actually acquired it. Its own doc comment says
`READY - Take success`.

**Why it did not show sooner.** `ch32h4_console_lock()` spins
`while (!try_lock())`. With the inverted helper the first read acquired the
semaphore and was reported as a failure, so the loop went round once more, the
second read reported success, and the lock was in fact held. It worked by
accident. Under contention it did not: the other core's value never matches
ours, so the loop spun out its entire bounded guard -- about a second -- and
then printed anyway.

**Fix.** `ch32h4_mutex_try_lock()` reads `HSEM->RLRX[id]` directly and succeeds
on zero. `HSEM_FastTake()` is not used.
