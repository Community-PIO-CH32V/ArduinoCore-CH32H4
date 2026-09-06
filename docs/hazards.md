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

---

## An RCC reset de-assert can be dropped, and the block stays in reset

**Symptom.** `SD.begin()` works once. The second call times out, and so does
every one after it, until the board is reset. The command trace is **empty** --
not a failed CMD0, none at all.

**Cause.** `RCC_HBPeriphResetCmd` is a read-modify-write with no read-back of
its own, exactly like `RCC_*PeriphClockCmd`. Nothing pushes the store out, and
the access that follows can be dropped. In `sd_controller_reset()` the write
that is lost is the reset **de-assert**, so the block stays held in reset, the
`CONTROL` write goes nowhere, and the controller never runs a command.

The first bring-up works because the block had never been reset. Only a board
reset clears it, because only a board reset releases the block.

**How it was found.** By bisection that ruled out the whole teardown path --
reducing `ch32h4_sd_end()` to nothing but clearing a flag still reproduced it
-- and then by the command trace being empty, which says the command never
reached the bus rather than the card refusing it. That trace facility was in
the MicroPython driver behind an `#if` this port had left non-compiling; it is
now always built, because a card that will not identify is close to
intractable without it.

**Fix.** A read-back after every RCC write in `ch32h4_sdmmc.c`. The same
applies anywhere else a peripheral is reset or gated at run time -- the console
and RNG drivers already read back after the clock enable, and this is the same
hazard on the reset register.

---

## Cutting SDCLK while a card is programming wedges the card

**Symptom.** A loop of bulk-write then re-initialise survives two rounds and
fails on the third. After that the card ignores CMD0 as well, so every
`begin()` times out in ACMD41 until the board is power-cycled.

**Cause.** A card holds DAT0 low while it writes, and the SD specification
requires the clock to keep running until it lets go. `ch32h4_sd_end()` stopped
SDCLK immediately.

**Fix.** Wait for DAT0 -- bounded, because a card that never releases it must
not hang a teardown -- before stopping the clock. `begin()` also retries
identification once from a full controller reset, since a card left mid-command
by an earlier session can ignore the first CMD0 and answer the second.

**Note.** MicroPython does not hit this because it never re-initialises within
a session. An Arduino sketch calling `SD.begin()` twice does, and that is
ordinary.

---

## The RTC counter is unsigned seconds from 2000, not a Unix timestamp

**Symptom.** None until 19 January 2038.

**Cause.** WCH's own RTC example stores a Unix timestamp in the 32-bit counter
and converts it with a signed `time_t` (openwch/ch32h417 issue 11).

**Fix.** The counter holds **unsigned** seconds since 2000-01-01, which runs to
2136 and needs no offset table, no overflow interrupt and no shadow copy. The
signedness was the whole bug; fixing that is worth more than re-basing the
epoch to buy a few decades. Conversion to and from the Unix epoch is one
addition, in `ch32h4_rtc.c`.

---

## Every backup-domain write must be read back, and BDRST is asserted at power-on

**Symptom.** The RTC cannot be started at all: every write to `BDCTLR` reads
back as zero and nothing reports an error.

**Cause.** Two things conspire.

The backup domain runs from a far slower clock than the 400 MHz core, and a
write that arrives while the previous one is still crossing is dropped
silently. WCH's own example gestures at this with a bare NOP delay inserted
only for the non-V3F cores. A fixed delay is a guess.

And this part comes out of power-on with **BDRST already asserted**, unlike
STM32 -- so every `BDCTLR` write is dropped until it is cleared. Since
`RCC_LSEConfig()` writes a whole byte of `BDCTLR` including the `RTCSEL` field,
merely starting the LSE counts as writing `RTCSEL = 00` and latches it there.
After that the clock can never be selected.

**Fix.** Read back until the value is there, pulse BDRST before selecting a
source, and start the oscillator **before** writing `RTCSEL`. Losing the second
half of the BDRST pulse leaves the domain held in reset, which no later write
can recover -- so that pair is read back too. Every flag wait is bounded, where
the SDK's `RTC_WaitForLastTask` and `RTC_WaitForSynchro` spin forever if no
oscillator is driving the domain.

**Measured**, over 20 s against the host clock: LSE −0.007 s, HSE/512 +0.007 s
(both at the measurement noise floor), and **LSI +0.655 s, or +3.3%** -- the
internal RC runs near 41.3 kHz against a nominal 40000 divisor. That is minutes
a day. LSI is for elapsed time, not for a clock.

---

## The I2S divider cannot reach common sample rates

**Symptom.** `begin(8000)` returns false. So does 16000, and 22050.

**Cause.** The divider is `2*I2SDIV+ODD` with `I2SDIV` at most 255, so the
largest division is 511 -- and it divides SYSCLK, which is 400 MHz. In 16-bit
mode a frame is 32 bus clocks, so the slowest reachable rate is
400e6/(32*511), about **24.5 kHz**. 32-bit mode halves that to about 12.2 kHz.

The I2S clock source is selectable per peripheral (`RCC_I2S2CLKSource`) but the
alternative is the PLL, which is faster still. There is nothing to switch to.

44.1 and 48 kHz are comfortable, and 44.1 lands within 1564 ppm -- about three
cents, against the twenty-one cents the SAI's 6-bit divider could manage.

**Fix.** `minimumFrequency()` and `maximumFrequency()` report the range, and
`begin()` returns false rather than clocking a rate nobody asked for.

---

## A throughput measurement that counts what was queued, not what was sent

**Symptom.** An I2S transmit path measured at 46208 frames per second against a
divider set to 44169 Hz -- 4.6% out, which looks like a clocking bug.

**Cause.** The measurement counted frames **written into the ring buffer**. A
run that starts with an empty ring absorbs one bufferful -- 2048 frames -- that
never reached the wire. Over one second that is the entire discrepancy; over
four it converged to 44672, and subtracting the queued frames gives 44160
against the divider's 44169.

**Not a hardware bug at all**, but worth writing down: any measurement of a
buffered path has to subtract what is still in the buffer, and the error looks
exactly like a rate error.

---

## mbedTLS 3.6 cannot be configured from nothing

**Symptom.** A config file written from scratch fails `check_config.h` with a
dozen "defined, but not all prerequisites" errors, none of which names a
prerequisite.

**Cause.** 3.6 routes hashes, ciphers and key types through PSA as well as the
legacy switches, so a module can have prerequisites in two places at once.

**Fix.** Start from upstream's own `mbedtls_config.h` and put every change in
one block at the end. Editing a working config is tractable; building one is
not. A version bump is then a fresh copy of upstream's file plus that block.

Two other things that are not optional on bare metal: `MBEDTLS_PLATFORM_MS_TIME_ALT`,
because `platform_util.c` has implementations for POSIX and Windows and
`#error`s on anything else; and compiling the port layer from the build script
rather than leaving it in `libraries/`, because a sketch includes
`mbedtls/ssl.h`, which resolves to the submodule, so the dependency finder
never sees a reason to build the port and the link fails on
`mbedtls_aes_init`.

---

## A TLS client with an unset clock rejects every certificate

**Symptom.** Every HTTPS connection fails verification with flag `0x200`. The
CA is correct, the chain is correct, and the message is "the certificate
validity starts in the future".

**Cause.** The RTC counts from 2000-01-01 and, unless something has set it,
from the moment power was applied. Every certificate ever issued therefore
looks not yet valid.

**Fix.** Not in the TLS code -- sync the clock first, which is why the RTC and
SNTP are part of this core. But `EthernetClientSecure::verifyErrorString()`
appends "the RTC has not been set" when the future-validity flag is set and the
clock is unset, because the bare message sends people to look at their CA.
`tests/hw/test_tls.py` asserts on that sentence.

---

## There is no I2S3, and the second I2S drove the first one's pins

**Symptom.** None. `I2S(OUTPUT, 1)` returned true, the peripheral ran, the DMA
ran, the measured throughput was correct, and no call reported an error. Three
pins toggled -- the wrong three.

**Cause.** Two of them, and the first hides the second.

WCH's numbering: there are **two** I2S blocks and they are the audio halves of
SPI2 and SPI3. The datasheet calls them **I2S1 and I2S2**, so I2S1 lives in the
SPI2 registers and I2S2 lives in the SPI3 registers. There is no I2S3 anywhere
on the part. This driver had been written as I2S2/I2S3, which is off by one
against the datasheet and exactly right against the register map -- so the
peripheral selection, the RCC gate and the DMA request numbers were all
correct, and only the names were wrong. That is the sort of error that survives
review, because everything it touches works.

What did not work was the pin setup: it hardcoded instance 0's three pins, on
GPIOB, with one alternate function for all three. Instance 1 therefore clocked
SPI3 while configuring SPI2's pads.

The alternate function is per **pin**, not per peripheral. Instance 0 is AF5 on
all three of its pins -- which is why a single constant worked and why nothing
suggested it would not generalise. Instance 1 is AF6 on clock and word select
but **AF7** on data.

**Fix.** A pin-and-AF table per instance in the variant, named the way the
datasheet names them, and `setBCLK()`/`setDATA()` checked against the
instance's own pins rather than instance 0's. Instance 1 defaults to PA15,
PB3 and PB2 -- what the mux offers once PA4 (the DAC output) and PB5 (OneWire)
are excluded.

**Worth noting** that an unwired instance is still measurable: the DMA feeds
the peripheral and the peripheral clocks its pins whether or not anything is
listening, so the divider and the throughput can be checked with no device
attached.

---

## analogRead() shares one ADC with ADCInput, and lost

**Symptom.** `analogRead()` returned a plausible, stable, wrong number --
about 1504 where 1673 was expected. Only after a paced capture had run; on a
freshly booted board it was correct.

**Cause.** `analogRead()` configured the ADC once, behind a static flag, and
never again. `ADCInput` reconfigures the same ADC for scan mode, with an
external trigger and DMA and a whole channel list in the regular sequence, and
leaves it that way. A software conversion started against that setup converts
the **whole sequence** and signals EOC at the end of it, so `analogRead()`
returned the last channel of somebody else's scan.

The number was 1504 because the previous test had captured `[ATEMP, AVREF]`,
and AVREF really does read 1504. A real conversion of a real channel, just not
the one that was asked for -- which is why it went unnoticed until something
read a channel whose value was known independently.

**Fix.** Re-establish single-shot mode on every read rather than once, and
refuse outright while a capture owns the sequencer. Refusing is the honest
answer for the second case: converting underneath a running capture would both
answer with the wrong channel here and drop a scan there, and a dropped scan
puts every later sample of the capture on the wrong channel.

**The general form** is worth keeping in mind: any two drivers sharing one
peripheral need the second one's configuration to be re-established rather than
assumed, and "initialise once" is the bug. The ADC, the timers and the DMA
channels on this part are all shared this way.

---

## The internal ADC channels need the slow sample window, or they read the channel before them

**Symptom.** A scan of `[A0, ATEMP]` reports a temperature channel that tracks
A0. With A0 near zero it reads as an implausibly cold die; with A0 mid-scale it
reads as a plausible warm one.

**Cause.** Both internal channels -- the temperature sensor and the 1.20 V
reference -- are driven through a high impedance and need a long sample window
to charge the sample-and-hold. Sampled with the short window a pin uses, they
return whatever the capacitor still held, which in a scan is the previous
channel's value.

**Fix.** The sample time is chosen per channel: the slowest setting for the
internal ones, a short one for pins. That makes the achievable rate depend on
the channel **list** rather than its length -- two internal channels cap a scan
near 24.8 kHz where two pins reach 152 kHz -- so `maximumFrequency()` computes
from the list, and `begin()` refuses a rate above it.

Refusing matters: an over-triggered ADC does not slow down, it **drops the
trigger**, and one dropped scan puts every later sample on the wrong channel
for the rest of the capture.

**Testing it** needs a known input on the channel before the internal one.
There is no signal generator on this bench, but PC0 and PC1 go nowhere, and a
pin can be driven from its own GPIO output driver while the ADC samples the
pad. Driving A0 to zero and requiring ATEMP to stay near its own value is what
separates a correct capture from one reading the previous channel -- and
driving the pair to opposite levels, both ways round, is what makes channel
ORDER checkable at all.

---

## Six of this package's sixteen analog inputs were missing from the variant

**Symptom.** `analogRead(PA4)` returned 0. So did PA5, PA6, PA7, PB0 and PB1.
No error, because returning 0 for a pin with no ADC input is the documented
behaviour -- the variant genuinely believed they had none.

**Cause.** The pin table gave `adc_channel = 0xFF` for all six. The datasheet
gives ADC1 channels 0-15 as PA0-PA7, PB0-PB1 and PC0-PC5, and the pin
definition table's third column confirms every one of them is bonded out on
the QEU6 package. `NUM_ANALOG_INPUTS` said 10 where the part has 16.

**Fix.** The six channels added, and `A10`..`A15` **appended** rather than
renumbered into place. Renumbering to put them in channel order would have
been tidier and would have silently changed which pad every existing
`A0`..`A9` sketch reads.

Several of the new pins carry a board function too -- PA4 is DAC1, PA5 is DAC2
and the SPI clock, PA6/PA7 are the SPI data pins and the loopback jumper. That
is board wiring rather than silicon, so the channels are declared and the
choice is left to the sketch, which is what the variant's own header comment
says it does.

**Worth noting** that this blocked something else: PA4 and PA5 being ADC
inputs is exactly what makes the DAC testable with nothing wired to the board.

---

## analogWrite() on a DAC pin cannot produce PWM, and both DAC pins have timers

**Not a bug -- a deliberate trade, recorded because it surprises people.**

`analogWrite()` sends a DAC-capable pin to the DAC and every other pin to a
timer. That is what STM32duino and the SAMD core do, and it is why there is no
`dacWrite()` here: a sketch written for either of those works unchanged. Only
ESP32 has a separate call.

The cost on this part is that **PA4 and PA5 both have timer channels**, and
neither can produce PWM through `analogWrite()` any more. PA5 matters most: it
is the SPI1 clock and one of only two pins on the 3.3 V rail with a timer.

A sketch that genuinely wants PWM there can still have it through
`ch32h4_pwm_find()` and the timer API. What it cannot have is `analogWrite()`
guessing which of the two was meant.

**Scaling** follows `analogWriteResolution()` on both paths. The rescale to the
converter's 12 bits multiplies rather than shifts, so full scale at 8 bits is
4095 and not 4080.

---

## The DAC output buffer does not cost range on this silicon

**Symptom.** None -- this is a hazard in the other direction: a driver written
from STM32 habit disables the buffer by default, giving up its drive strength
to avoid a limitation this part does not have.

**Cause.** On classic STM32 the DAC's output buffer cannot come within about
0.2 V of either rail, so a buffered channel told to output zero sits near 250
counts of 4095. Everybody who has met an STM32 DAC knows this.

**Measured here**, reading the DAC's own pad back through the ADC -- PA4 is
ADC4, so this needs nothing wired to the board:

| | code 0 | code 4095 |
|---|---|---|
| buffered | 1 | 4090 |
| unbuffered | 0 | 4089 |

About one count of difference at the bottom and none worth reporting at the
top. The buffered output is effectively rail to rail.

**So** the buffer stays on by default and `ch32h4_dac_output_buffer()` exists
for drive strength rather than for range. Note what the measurement cannot
say: the ADC input is a high-impedance load, which is precisely the load an
unbuffered DAC copes with best, so nothing here compares drive strength.

---

## The USB buffers did not need to be in USB_RAM, and the linker rule that put them there was matching nothing under arduino-cli

**Symptom.** None, in either direction. That is what makes this one worth
writing down: a placement rule that had never fired under one of the two build
systems, guarding against a failure that does not happen.

**Cause.** Two separate mistakes that hid each other.

The linker script swept TinyUSB's `.bss` into `USB_RAM` with
`*tusb*.o(.bss .bss.* COMMON)` -- matched on the object FILENAME, because
PlatformIO's build script renames every TinyUSB object to `tusb*.o` for exactly
this. arduino-cli names them after their sources: `usbd.c.o`,
`dcd_ch32_usbfs.c.o`. So under the Arduino IDE the rule matched nothing at all,
`.usbram` was zero, and every endpoint buffer sat in DTCM.

And the reason given for the rule -- "the USB controller's own bus master
cannot see DTCM, so buffers left there give a device that enumerates and then
transfers nothing" -- is not true. It is true of the Ethernet DMA, which is why
`.ethram` exists and is verified. It was assumed of USB by analogy and never
tested.

**How it was found.** Turning `-flto` on. LTO replaces the object names with
`ccXXXXXX.ltrans0.ltrans.o`, so the filename rule stopped matching under
PlatformIO too and `tests/test_link_matrix.py` failed on `.usbram` being zero
-- a test written for a hazard that turned out not to exist, catching a rule
that turned out not to work.

Then, rather than trusting either statement: an arduino-cli build was flashed
and USB CDC came up and transferred, with `.usbram` at zero. And a PlatformIO
build with both placement rules removed -- every TinyUSB buffer in DTCM --
passed all seven of `tests/hw/test_usb.py`.

**Fix.** `CFG_TUSB_MEM_SECTION` is defined as `__attribute__((section(".usbram")))`
in `cores/ch32h4/tusb_config.h` and in the TinyUSB fork's
`tusb_config_ch32h4.h`, and the CH32 device driver marks its endpoint buffer
struct with it. Placement is now by section, which means the same thing under
both build systems and under LTO. The filename rule stays as a bonus sweep
where it applies.

The placement itself is kept, but it is now documented as what it is: `USB_RAM`
is 8 KB of shared memory nothing else claims, so it is a few kilobytes of DTCM
back. Not a correctness requirement. `ch32h4_usb_init()` zeroing the region
before use IS one, because `.usbram` is `NOLOAD` and outside the `_sbss.._ebss`
range the startup code clears.

**The lesson worth keeping.** Two of these hazards -- this one and `.ethram` --
have the same shape and only one of them is real. "This master cannot reach
that memory" is a claim about silicon, and on this part it differs per master.
Test it per master; do not carry it across by analogy.

**Where.** `variants/CH32H417xx_QEU6/ch32h417.ld`,
`cores/ch32h4/tusb_config.h`,
`libraries/Adafruit_TinyUSB_Arduino/src/arduino/ports/ch32h4/tusb_config_ch32h4.h`,
`libraries/Adafruit_TinyUSB_Arduino/src/portable/wch/dcd_ch32_usbfs.c`.

---

## Programming a flash page hangs the part if the other core is running

**Symptom.** `ch32h4_flash_write()` does not return. Not a fault, not corrupted
data — the board simply stops, and only the watchdog gets it back. Erasing is
fine; it is the page *program* that never completes.

It needs a sketch that uses the second core. A sketch with no `loop1()` leaves
the V3F asleep in stop mode, where it fetches nothing, and everything works —
which is why LittleFS and EEPROM have always passed their tests.

**Cause.** Both cores execute XIP from the same flash array, and only one of
them is protected. The V5F has an instruction cache, so its programming loop
runs from cache while the array is busy; `flash_program_page()` is in ITCM as
well. **The V3F is in-order and has no instruction cache**, so it fetches every
instruction it executes from the flash the other core is writing.

`ch32h4_flash_erase()` is not even in ITCM — it calls the SDK's
`FLASH_ErasePage()` from flash — and gets away with it for the same reason.

**Measured**, on the `dualcore` sketch, with the V3F running an ordinary
flash-resident `loop1()`:

| operation | V3F parked in ITCM | V3F running from flash |
|---|---|---|
| erase one 8 KB page | ok, 49 042 spins through it | ok, 197 loops through it |
| erase + program one 256 B page | ok, 69 120 spins | **hangs**, IWDG resets it |
| erase + program 128 KB | ok, 2 111 157 spins, verified | hangs |

The step markers are printed and flushed before each operation, so the last
line on the wire names the one that did it:

```
fs_parked=0
fs_step=erase-one-page
fs_step=program-one-page      <- last line
rst=0x20000000 iwdg
```

No fault is recorded on either core. It is a hang, not a trap.

**Consequences beyond OTA.** A dualcore sketch that writes LittleFS or EEPROM
from `setup()`/`loop()` while `loop1()` is running will hang. That is a live
hazard in shipped code, not only a constraint on a future updater.

**Fix, and it is in.** `ch32h4_flash_erase()` and `ch32h4_flash_write()` park
the other core themselves, so LittleFS, EEPROM and anything else that writes
flash are safe without knowing any of this exists. The two cases that used to
hang — one page, and 128 KB — now erase, program and verify with `loop1()`
running throughout.

It is done **by interrupt**, not by a flag the other core polls. A cooperative
check was written first and it is not good enough: `loop1()` belongs to the
sketch and may run for a long time without returning or yielding, so the park
would quietly time out and turn a filesystem write into a failure nobody asked
for. That is a worse bug than the one being fixed, because it is silent.

Two things had to be checked rather than assumed, and the first was got wrong
once:

* **The V3F does take interrupts.** `startup_v3f.S` programs `mtvec` and
  `mstatus` like any other core. Its vector table was sixty copies of
  `Stray_IRQ_v3f` only because nothing had ever needed an entry — a
  placeholder, not a limitation. Slots 18 and 19 are real now, and the rest of
  that table is the obvious place for anything else that core should service.
* **That table is in SRAM**, `V3F_VECTOR` at 0x2010D000, not in flash. So
  taking the interrupt needs no flash read — which is what makes the whole
  arrangement possible. Had the table been in flash, this could not have worked
  at all.

The handler spins in ITCM with interrupts masked, because a SysTick landing on
the parked core would find its vector in SRAM and its handler in flash.

`cores/ch32h4/ch32h4_park.c`. `tests/sketches/dualcore` keeps the manual
`flash ... parked` form, which is what established the difference in the first
place.

**Fix.** Park the other core in ITCM for the duration. `tests/sketches/dualcore`
has a working demonstration: a request flag and an acknowledgement in `.xcore`,
and an `__itcm_func` spin loop the V3F sits in so that it touches no flash at
all. With that in place a 128 KB erase-and-program completes and verifies while
the V3F spins two million times.

Doing it cooperatively — a flag the other core notices — only works if that
core reaches the check, which `loop1()` need not. The general fix is an
ITCM-resident IPC interrupt handler that parks whichever core it lands on,
called automatically from `ch32h4_flash_erase()` and `ch32h4_flash_write()`.

**The lesson worth keeping.** "Both cores run from one flash array" is not a
symmetric statement when one of them has a cache and the other does not. Every
test that made flash writes look safe was run with the second core asleep.

**Where.** `cores/ch32h4/ch32h4_flash.c`, `tests/sketches/dualcore/src/main.cpp`,
`tests/hw/test_dualcore.py`.

---

## Multicast reception never worked, and mDNS is how it showed up

**Symptom.** The mDNS responder announces perfectly — a packet capture shows a
well-formed `ch32h4.local` A record leaving the board — and answers no query at
all. Everything on the board reports success: the netif is up, `NETIF_FLAG_IGMP`
is set, `mdns_resp_add_netif()` and `mdns_resp_add_service()` both return OK.

**Cause, and there were two.**

The MAC was configured `ETH_MulticastFramesFilter_Perfect`, which compares
against the MAC address registers. Those hold this interface's own address and
nothing else, so *every* multicast frame was dropped in hardware. mDNS lives on
01:00:5E:00:00:FB. The queries never reached lwIP.

And `NETIF_FLAG_IGMP` was set without an `igmp_mac_filter` callback, so even
with a hash filter lwIP had no way to tell the hardware which groups to accept.
The flag says "this interface can do multicast"; the callback is what makes it
true.

**The bit that cost an afternoon.** With hash filtering enabled and a filter
callback installed, it still did not work. The hash bin is *not* the top six
bits of the CRC32 of the destination MAC, which is the obvious reading of every
datasheet that says "the upper 6 bits of the CRC". It is the top six bits of the
**bit-reversed** CRC — what Linux's stmmac driver and ST's HAL both compute.

For 01:00:5E:00:00:FB the naive formula gives bin 30 and the hardware wants bin
48. Found by opening every bin (`MACHTHR = MACHTLR = 0xFFFFFFFF`), watching mDNS
start answering, then setting one bin at a time until it answered again.

**Fix.** `ETH_MulticastFramesFilter_HashTable`, an `eth_igmp_filter()` installed
with `netif_set_igmp_mac_filter()`, and the reversed-CRC index. The bins are
reference-counted, because several groups can hash to one bin and leaving one
group must not deafen the interface to another that collided with it.

**What else this was breaking.** Anything multicast: SSDP, mDNS, and any
sketch joining a group with `igmp_joingroup()`. Nothing tested multicast
reception before, which is why a driver that could not receive it at all
passed everything.

**Where.** `libraries/lwIP_Ethernet/src/ch32h4_eth.c`.

## Next-line branch prediction makes the V5F execute the wrong code

**Symptom.** A wild pointer deep inside lwIP, on every boot, at the same
address — but only for some builds. Any unrelated change moves it or makes it
disappear: a compiler flag, an added string, enabling an assert. It was first
found as "`-mno-save-restore` stops the board booting", which is not what it is.

```
=== TRAP ===
mcause=0x00000004 mepc=0x0000eeea mtval=0xe339e345
sp=0x200def70 ra=0x0000ee5a a0=0x000cf050 a1=0x0000eea2
```

**Cause.** `corecfgr` (CSR `0xBC0`) bit 15, `NLP_EN`, next-line branch
prediction. The core mispredicts and resumes execution at an address it was
never sent to. Measured three ways on one unchanged build:

| I-cache | NLP | result |
|---|---|---|
| on | on | faults, deterministically |
| off | on | runs (NLP does nothing without the cache) |
| on | off | runs, at full cached speed |

The middle row is why this looks like a cache bug and is not one. Disabling the
cache masks it, because the predictor only feeds a cached fetch path.

**What it actually did.** Execution ran off from the middle of `memp_malloc()`
into the middle of `memp_free()` — `0xF8` further on, between two
byte-identical `sh2add` instructions — so lwIP's pool index was computed twice:

```
edec:  sh2add a0,a0,a5   ; memp_malloc: a0 = &memp_pools[7] = 0x2967C
                         ;   ...execution resumes here...
eee4:  sh2add a0,a0,a5   ; memp_free:   a0 = memp_pools + 0x2967C*4 = 0xCF050
eee8:  lw a5,0(a0)       ; unprogrammed flash -> 0xE339E339
eeea:  lw a5,12(a5)      ; FAULT
```

The arithmetic identifies it beyond doubt: `a0 = 5*memp_pools + 112` held in
every build observed, which is `&memp_pools[7]` fed through `sh2add` a second
time. The register state is the tell — `ra` belonged to one function and `pc`
to another, which no legitimate call can produce.

**Fix.** Do not set bit 15. `startup_v5f.S` composes `CORECFGR_V5F` without it;
`-DCH32H4_V5F_NLP` puts it back for anyone wanting to re-measure. WCH's own
startup sets it, so either their silicon needs a workaround we do not have or
the feature is broken on this part. The cache stays on, so the 145x above is
kept.

**What this cost, and the lesson.** Four bench rescues and a long detour
through lwIP. Ruled out first, each by measurement rather than argument:
`MEMP_OVERFLOW_CHECK=2` and `MEM_OVERFLOW_CHECK=2` silent through a 144 KB
transfer; no ISR touches lwIP; all 31 interrupt handlers correctly attributed;
`sp` 192 bytes below `_estack_v5f`, so no overflow; the `xw` extension
innocent; and flash byte-identical to the ELF. **A fault that moves when you
change something unrelated is not a memory bug — look at the fetch path.**
The fault record now captures `ra`/`a0`/`a1`, which is what finally cracked it,
and `-DCH32H4_LWIP_ASSERT_CONSOLE` wires lwIP's asserts to the raw UART.

## A spin loop wedges the debug probe, including the one guarding against crashes

**Symptom.** The board is unreachable: `openocd` reports "WCH-Link failed to
connect with riscvchip", and only NRST held through a `wlink erase` recovers
it. Looks exactly like a bricked part.

**Cause.** `main_v3f.c` stopped waking the V5F after
`CH32H4_FAULT_REBOOT_LIMIT` consecutive faults, printed "reflash, or reset
twice to try again", and then sat in `for (;;) {}`. A core spinning at full
clock wedges the WCH-Link — which `ch32h4_fault_dump()` already warned about,
and is why the fault handler resets rather than looping. So the guard meant to
keep a faulting board reachable was the thing making it unreachable, and its
advertised escape hatch needed the probe it had locked out.

**Fix.** `for (;;) { __WFI(); }`, in both of that file's fatal paths. Verified:
with it, the probe attaches to a fault-guarded board and can dump all 182 KB of
flash, which was impossible before.

**Use the right wlink: `tools/bin/wlink.exe`, 0.1.2.** PlatformIO's
`packages/tool-wlink/wlink.exe` is **0.1.1**, which reports "Probe is not
attached to an MCU, or debug is not enabled" on this part regardless of how
healthy it is. `tests/hw/conftest.py` has said so since the harness was
written, and pins 0.1.2 for exactly this reason.

Believing 0.1.1 cost four NRST-and-erase rescues of a board that was fine —
`openocd` attached to it and examined both harts in the same minute — and
produced a confident but wrong story about wlink lacking CH32H417 support,
inferred from CH32H417 not appearing in `wlink --chip`. It does support it:
`Community-PIO-CH32V/wlink` is a fork carrying that support, and the Boards
Manager index ships it.

**wlink is the right tool to PROGRAM with**, and openocd is not: openocd fails
to program the higher flash addresses, which is why the hardware harness uses
wlink. That was established the hard way in the MicroPython and libhal work
before this core existed. openocd is fine for *attaching* — which is what makes
it a good second opinion when wlink says nothing is there.

## USB mass storage: the stall is fine, the handover signal is not

**Measured**, exposing the internal flash FAT volume over USB MSC and copying
files from a Windows host:

| | |
|---|---|
| worst-case stall inside a single MSC write | **1.3 ms** (peak 2.6 ms under load) |
| enumeration | `CH32H4 Flash Filesystem`, 216 KB, MBR, Online |
| mounted volume | FAT, 198 KB, read and written both ways |

The stall was the thing worth worrying about in advance: an MSC write lands in
the TinyUSB task and calls into flash, which parks the other core and masks
interrupts for the duration of an 8 KB erase. At a millisecond or two, bulk
transfers absorb it. No RAM staging is needed.

**The handover signal is the real problem, and it is a host behaviour rather
than a bug here.** `FatFSUSB`'s `onPlug`/`onUnplug` come from SCSI START STOP
UNIT and PREVENT/ALLOW MEDIUM REMOVAL. Windows was measured mounting the
volume, writing a file and ejecting it **without sending either** -- the plug
and unplug counts stayed at zero throughout. Two implementations were tried
before concluding this: overriding `tud_msc_scsi_cb` the way arduino-pico does
(never called -- TinyUSB answers those commands itself and only forwards
what it does not handle), and TinyUSB's dedicated
`tud_msc_prevent_allow_medium_removal_cb` (correct hook, but Windows does not
send the command).

**The override was checked to actually link before that conclusion was drawn**,
and it is worth saying why the check is not optional. A weak symbol is already
*defined*, so the linker has no undefined reference to satisfy and will not
pull a replacement object out of an archive merely to override it. A library
built with `dot_a_linkage` (Arduino) or PlatformIO's default `lib_archive`
therefore loses the override silently -- and "the host never sent the command"
then looks identical to "the override never linked". `nm` on the archive member
showed `T tud_msc_prevent_allow_medium_removal_cb` against TinyUSB's `W`, and
disassembly of the final image showed the function inlined into
`mscd_xfer_cb`, incrementing its call counter. It links; the host does not send
it. Without that check the conclusion above would have been a guess.

The corollary for anyone editing FatFSUSB: do not move that callback into a
translation unit nothing else references, or it becomes needed only for the
override and stops being linked.

That matters because a sketch and a host must never both have a FAT volume
mounted: FatFs caches directory and allocation sectors, and a host writing
underneath that cache corrupts one or both views silently.

**So `FatFSUSB::hostWrites()` exists**, counting sectors the host has written.
A write is a write whatever the host announces, so a sketch can see that its
cached view is stale and remount. The callbacks stay as a fast path where a
host does send them; `hostWrites()` is the backstop, and the USBDrive example
uses both.

**A demonstration of what happens without it**, seen during development: a test
sketch kept the volume mounted while the host wrote to it, and on the next boot
the volume was inconsistent enough that `begin()` auto-formatted it -- the
host's file simply gone. That is the failure the contract exists to prevent,
and it is silent.
