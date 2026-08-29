# An Arduino core for the CH32H41x

Date: 2026-08-29
Target board: `CH32H417QEU6-R0-1v1` (chip `CH32H417QEU8`), QingKe V3F + V5F
Status: approved design

This document covers the whole-program architecture at the level needed to
start, and then specifies **Milestone 1** in full. Later milestones get their
own specs; what is here about them is scope, not design.

## 1. Goal

An Arduino core for the CH32H41x that implements the
[ArduinoCore-API](https://github.com/arduino/ArduinoCore-API), builds under
PlatformIO, runs baremetal on the WCH SDK with no RTOS, and puts user code on
the **400 MHz V5F** core by default. It follows
[arduino-pico](https://github.com/earlephilhower/arduino-pico) wherever a
convention already exists there — dual-core `setup1()`/`loop1()`, the `lwip_*`
library structure, the C++ exceptions build menu.

Two prior ports to this silicon are the evidence base and are cited throughout:
a libhal v5 port (`ch32h417_async`, V3F, Ethernet + lwIP + mbedTLS + TLS) and a
MicroPython port (`ch32h4_micropython`, V5F, USB CDC + MSC, filesystem,
Ethernet, mbedTLS). Neither has run user code on both cores.

### 1.1 Non-goals

- FreeRTOS, or any RTOS.
- Boards other than the one above. One variant.
- Two ELF files. See §3.
- Preserving compatibility with the existing `arduino_core_ch32` core, which is
  stm32duino-derived and not ArduinoCore-API based.

## 2. Verified facts

Established by direct measurement in this session or by the two prior ports.
Three of these contradict `ch32h417_async/docs/PORTING.md`, which is otherwise
the best single source on this silicon.

### 2.1 The toolchain

`platformio/toolchain-riscv` **1.120200.220829** is WCH's xPack-derived GCC
12.2.0 (`riscv-wch-elf-*`), and it is what this core uses. Verified in this
session, against the claim in `PORTING.md` §03 that exceptions cannot work on
it:

| Library | `.eh_frame` in `eh_throw.o` |
|---|---|
| `libstdc++.a`, all 18 multilibs | **present** |
| `libstdc++_nano.a` | **absent** |

So `PORTING.md`'s "in any multilib" is true of `libstdc++_nano.a` and of WCH
GCC 15, not of this package's regular `libstdc++.a`. Exceptions are available;
`--specs=nano.specs` is **banned**, because it silently rewrites `-lstdc++` to
`-lstdc++_nano` and every `throw` then reaches `std::terminate` at run time
after a clean link.

The registry also carries **1.130200.2**, which is the genuine upstream xPack
`riscv-none-elf-gcc` 13.2.0 (binutils 2.41), for Windows, Linux and macOS. It
was evaluated and **not** chosen: WCH's vendored startup and `core_riscv.c` use
`__attribute__((interrupt("WCH-Interrupt-fast")))` and the custom `xw`
extension, and no port has yet run generic-toolchain output on this silicon —
MicroPython's `CH32_TOOLCHAIN=generic` build is a compile-time guard that is
never flashed. Keeping WCH's GCC retires that risk for free. **No new toolchain
repository is needed.**

Flags:

```
-march=rv32imafc_zba_zbb_zbc_zbs_xw  -mabi=ilp32f
-ffunction-sections -fdata-sections
-Wl,--no-relax-gp        # gp relaxation breaks above ~305 KB of RAM code,
                         # and reports a relocation rather than a size
-Wl,--print-memory-usage
```

`ilp32f` is a hard-float ABI: mixing `ilp32` objects with it is a link error at
best. Both cores have the FPU (`misa = 0x40901127`).

### 2.2 The memory map, and who can reach what

| Region | Base | Size | Notes |
|---|---|---|---|
| ITCM | `0x200A0000` | 128 KB | V5F tightly-coupled, zero-wait at core speed. V3F reaches it with HCLK waits. |
| DTCM | `0x200C0000` | 256 KB | V5F tightly-coupled, zero-wait. V3F reaches it with HCLK waits. |
| Shared SRAM | `0x20100000` | 512 KB | Zero-wait at HCLK. **The only memory both cores reach at speed.** |
| Code flash | `0x08000000` | 960 KB | Alias at `0x00000000`. ~25 MHz equivalent, behind the V5F's 32 KB I-cache. |

ITCM and DTCM are *tightly coupled to the V5F*, not private to it: they are
globally addressable and the V3F can reach them, slowly. A single `.data` /
`.bss` shared by both cores is therefore sound.

**DMA1/DMA2 reach every region, including DTCM and ITCM.** `PORTING.md` says
DTCM is not DMA-reachable; that is wrong, and was corrected in
`ch32h417-notes.md` by experiment. The general-purpose DMA has its own
permission field, `MEMARY_CFGR[3:2]` for DTCM and `[1:0]` for ITCM (CSR
`0xBC5`), both reset to `0b11`. The restriction is real for the **USB
controller's own bus master**, which is why TinyUSB's buffers must live in the
shared region.

### 2.3 The clock tree

```
SYSCLK / SystemCoreClock  400 MHz   the V5F core clock
HCLK                      100 MHz   SYSCLK >> 2 -- the bus clock
ADCCLK                     12.5 MHz HCLK / 8
```

There is no APB prescaler in the STM32 sense; `RCC_ClocksTypeDef` has no `PCLK`
field. **SysTick counts at HCLK**, and SPI, the timers and I2C all divide HCLK.
`SystemCoreClock` is never the right number for a peripheral divider, and using
it is a factor-of-four error that no self-consistent test can detect.

On the V3F, `SystemCoreClock == HCLK`, so the same code is correct on both
cores only if it reads HCLK explicitly. It must.

`RCC->CFGR0` bits `[15:14]` are `ADCPRE`, but the SDK's `RCC_ADCPRE_ADCH_DIVx`
constants are at bits `[13:12]` and disagree with `RCC_ADCPRE`'s own mask.
Write the field by hand.

**The crystal decides whether Ethernet and USB exist.** Boot tries HSE and
falls back to the internal RC. On the RC the board runs, but the Ethernet PLL
never locks and USB is out of spec. The core announces which one it got.

### 2.4 There is a hardware mailbox

`PORTING.md` says there is no documented hardware mailbox. The SDK has two:

- `ch32h417_ipc.c` — 4 channels, `IPC_WriteMSG` / `IPC_ReadMSG`, per-channel
  interrupts. This is the `rp2040.fifo` equivalent.
- `ch32h417_hsem.c` — hardware semaphores with core and process IDs. This is
  the mutex equivalent.

Both are M4's foundation, and neither needed inventing.

### 2.5 Failures that produce no error

Every item here compiles, links and runs. They are the reason for the
defensive measures in §7.

| Cause | Symptom |
|---|---|
| Wrong RCC bus (`HB` / `HB1` / `HB2`, **not** APB1/APB2) | Registers read back as zeroes, writes discarded |
| No read-back after a clock enable | The first access to the peripheral is dropped |
| `AF_PP` without `GPIO_PinAFConfig` | Peripheral runs, flags correct, nothing on the wire |
| `GPIO_PinAFConfig` without the AFIO clock | The AF register write is discarded |
| No block reset at init | You inherit the previous run's configuration |
| Assuming erased flash is `0xFF` | It is `0xE339E339`; blank sectors look full |
| `--specs=nano.specs` | Every `throw` reaches `std::terminate` |
| A fault during static init | Total silence — no console exists yet |
| `WFI` with interrupts masked | Hangs this core; the RISC-V spec says otherwise |
| `0x55` anywhere in `wlink` output | The write before it never happened, and `Flash done` still prints |

The RCC bus assignment defies habit and is worth stating in full: `USART1` and
`SPI1` are on **HB2**; `SPI2`, `SPI3`, `I2C1`, `I2C2` are on **HB1**; `TIM1`
and `TIM8`–`TIM12` are on **HB2** but `TIM2`–`TIM7` are on **HB1**; `GPIOA`–
`GPIOF` and `AFIO` are on **HB2**; `DMA1/2`, `SDMMC`, `RNG`, `ETH`, `OTG_FS`
and `FMC` are on **HB**.

Peripheral configuration survives a warm reset, the debugger's reset and a
re-flash — only a power cycle clears it. Reset each block when initialising it,
**except** `GPIOx` and `AFIO` (shared by every driver), `PWR` and `BKP` (drops
the VIO18 rail, discards the backup domain), `DMA1` (channels are shared), and
`ETH` (hangs the boot in a way no delay fixes).

## 3. One ELF, two cores

Both prior ports ship two ELFs merged by a script. That was necessary for them
and is not necessary here.

**Why they needed two.** WCH's `startup_ch32h417_v3f.S` and
`startup_ch32h417_v5f.S` are near-identical and define the same ~150 symbols —
`_start`, `handle_reset`, `_vector_base`, and every weak IRQ handler. Linking
both collides on all of them. They differ in only three places: the V3F sets a
70 MHz preliminary PLL that the V5F must not repeat, the V5F toggles flash
cache control at `0x40022000` around its RAM copy, and the two write different
prefetch/nesting values to CSR `0xBC0` / `0xBC1`.

**Why we do not.** The core writes its own startup, so both sets of symbols are
prefixed (`_start_v3f` / `_start_v5f`, `_vector_base_v3f` / `_vector_base_v5f`,
and per-core handler tables). `NVIC_WakeUp_V5F()` accepts any address, masking
it with `~0x3FF`, so the V5F entry may live inside the same image provided it
is 1 KB aligned. One link, one `.bin`, no merge step.

That masking is itself a hazard: an unaligned entry starts the core in the
middle of whatever precedes it, with no complaint. The linker script asserts
the alignment.

### 3.1 Boot sequence

```
V3F reset @ 0x00000000
  |- set the VIO18 rail          PWR_CTLR[12:10] + bit [9], before ANY pin
  |- SystemInit()                full clock tree; HSE with HSI fallback
  |- early USART1 console        before anything can fault
  |- announce HSE vs HSI         loudly; it decides if Ethernet and USB exist
  |- NVIC_WakeUp_V5F(0x00008000)
  '- M1: PWR_EnterSTOPMode(...) forever
     M4: wait for the V5F's ready flag, then setup1() / loop1()

V5F entry @ 0x00008000          1 KB aligned; no stack, no vectors, no clocks
  |- sp = _estack_v5f;  gp = __global_pointer$
  |- copy .load to RAM, call it  (flash cache is toggled around the copy)
  |- SystemAndCoreClockUpdate()  NOT SystemInit -- never re-PLL a running core
  |- copy .data, zero .bss, copy .itcm_text
  |- CSR 0xBC0 = 0x1237B3E0, 0xBC1 = 7, 0x804 = 0x0F, mstatus = 0x6088
  |- mtvec = _vector_base_v5f | 3
  |- __register_frame_info(__eh_frame_start)   priority-101 ctor, if -fexceptions
  |- run .init_array
  |- signal "runtime ready"
  '- setup();  for (;;) loop();
```

The **V5F** performs the C runtime initialisation. The V3F stub touches only
its own small private state before the V5F is up, and in M4 blocks on the ready
flag before calling `setup1()` — the discipline arduino-pico uses for core 1.
`PWR_EnterSTOPMode` rather than a bare `WFI` in the stub's park loop, because
Stop mode requires *both* cores to request it and that helper clears its own
`SLEEPDEEP` on the way out.

### 3.2 Exceptions

A `boards.txt` menu, as in arduino-pico and the ESP cores. Default **disabled**.

Unlike arduino-pico, one `libstdc++.a` serves both settings — `-fno-exceptions`
only changes codegen for our own sources — so no second prebuilt library is
needed. Enabling costs roughly 30 KB of `.eh_frame` and `.gcc_except_table`,
which live in flash and are read only when something throws.

Under `-nostartfiles`, **nothing registers `.eh_frame`**: this libgcc uses the
registry-based FDE lookup and crtbegin's `frame_dummy` never runs. A
priority-101 constructor calls `__register_frame_info(__eh_frame_start, ...)`.
Without it, exceptions link and then fail at run time exactly as `nano.specs`
does.

## 4. Memory and flash layout

### 4.1 Flash — 960 KB user area

| Region | Address | Size | Contents |
|---|---|---|---|
| `FLASH_V3F` | `0x08000000` | 32 KB | V3F stub: vectors, clocks, VIO18, console, wake |
| `FLASH_V5F` | `0x08008000` | 912 KB | the Arduino image |
| `EEPROM` | `0x080EC000` | 16 KB | two 8 KB pages |

`FLASH_V5F` stops at the EEPROM base rather than running to the end of the
chip, so an oversized image is a **link error**. MicroPython lost 26 KB of
image into its filesystem for want of exactly this, and the board boot-looped
on the next reset after the first file was written.

### 4.2 RAM — XIP-primary

| Region | Base | Size | Contents |
|---|---|---|---|
| ITCM | `0x200A0000` | 128 KB | `.itcm_text` — measured-hot code only; remainder unused |
| DTCM | `0x200C0000` | 256 KB | `.data`, `.bss`, V5F stack, heap part 1 |
| Shared | `0x20100000` | 512 KB | 52 KB reserved buffers, heap part 2 |

Code runs XIP from flash behind the V5F's 32 KB I-cache. Only functions
measured as hot are placed in ITCM: the interrupt trampoline,
`digitalWrite`/`digitalRead`, `micros`/`millis`, `delayMicroseconds`, and the
fault handler. A `__not_in_flash_func()`-equivalent macro lets libraries and
sketches promote their own. Most of ITCM stays free by design.

The 52 KB reserved in shared SRAM is carved out in M1 so the map does not churn
under later milestones. The sizes are MicroPython's measured ones:

| Reservation | Size | Milestone |
|---|---|---|
| `USB_RAM` | 8 KB | M2 — **must** be here; the USB bus master cannot reach DTCM |
| `ETH_RAM` | 28 KB | M6 — 16 descriptors + 12 RX / 4 TX frame buffers |
| `SD_RAM` | 8 KB | M5 — two runs of eight 512-byte blocks, 16-byte aligned |
| V3F stack + cross-core data | 8 KB | M4 — shared SRAM is the only memory both cores reach at speed |

**Heap is about 660 KB**, served by an `_sbrk` that hands out DTCM's remainder
(~200 KB, zero-wait at 400 MHz) and then continues into the shared region
(~460 KB, HCLK-rate). newlib's dlmalloc starts a new segment when `sbrk`
returns non-contiguous memory rather than assuming contiguity, so this is
sound; no allocation ever spans the gap.

`printf` allocates its stdout buffer from the heap on first use. With this much
heap that is a non-issue, but it is why the heap is not sized to the minimum.

### 4.3 The measurement that could overturn §4.2

MicroPython found flash XIP "far worse than the 4x clock ratio implies for
anything loop-shaped", and copies 392 KB of `.text` into shared SRAM as a
result. That measurement is from an interpreter dispatch loop — the
pathological case for an I-cache — and an Arduino sketch is not that shape.

M1 carries a benchmark that measures XIP against RAM-resident code on this
board. If XIP loses badly, the fallback is a `RAM_CODE` region carved from the
shared half of the heap; it costs heap and nothing else, and no interface
changes. The layout is chosen on the number, not on the argument.

### 4.4 Two linker-script constraints that are not obvious

1. **Flash-resident sections must sit after `.dtors`.** Everything from `.fini`
   to `.dtors` is one linear flash-to-RAM copy performed by the startup stub. A
   flash-resident section placed inside that span still consumes LMA, so every
   later LMA shifts and `.init_array` is reconstructed from the wrong bytes.
   The result links cleanly and never boots, with no fault and no output.
2. **`(NOLOAD)` on `.stack`.** A section that only advances the location
   counter becomes `PROGBITS` at a RAM load address under some linkers, and
   `objcopy -O binary` then pads the image out to it. The libhal port produced
   a 514 MB `firmware.bin` this way.

## 5. Repositories

| Repo | Action |
|---|---|
| `arduino-core-ch32h4` | **new** — the core |
| `Community-PIO-CH32V/platform-ch32v` | new branch off **`develop`** (there is no `master`); adds `framework-arduinoch32h4`, the board, the builder script; drops the `framework-libhal-ch32h417` symlink |
| `maxgerhardt/ch32h417lib` | reused as-is, submodule at `system/ch32h417lib` |
| `maxgerhardt/tinyusb` branch `ch32h417-v2` | reused as-is, submodule (M2) |
| `arduino/ArduinoCore-API` | vendored copy, per the brief |
| toolchain | **none** — `platformio/toolchain-riscv` is already pinned |

```
arduino-core-ch32h4/
  ArduinoCore-API/          vendored, mirrored into cores/ch32h4/api
  cores/ch32h4/             Arduino.h, main.cpp, startup, drivers, api/
  variants/CH32H417QEU6/    pins_arduino.h, ch32h417.ld
  system/ch32h417lib/       vendor SDK submodule
  libraries/                Wire SPI EEPROM Servo I2S ADCInput Ticker SD SDFS
                            lwIP_* Adafruit_TinyUSB
  boards.txt platform.txt package.json
```

## 6. Milestones

Each is independently testable on hardware and gets its own spec.

| | Milestone | Rests on |
|---|---|---|
| **M1** | Boot, clocks, single-ELF dual-image, core Arduino API on the V5F | — |
| M2 | TinyUSB; `Serial` becomes USB CDC; Adafruit_TinyUSB | M1 |
| M3 | Wire, SPI, EEPROM, Servo, Tone, I2S, ADCInput, Ticker | M1 |
| M4 | `setup1()` / `loop1()` on the V3F; IPC FIFO; HSEM mutexes | M1 |
| M5 | SD, SDFS, FatFS | M3 |
| M6 | lwIP, on-chip Ethernet, arduino-pico's `lwip_*` structure | M2 |
| M7 | mbedTLS with the ECDC AES and TRNG accelerators | M6 |

M3 and M4 are independent of each other.

`Serial` is USB CDC from M2 onward, with `Serial1` the USART1 console and a
build menu to swap them. In M1, which has no USB, `Serial` is USART1.

## 7. Milestone 1 specification

### 7.1 Deliverables

| Area | Detail |
|---|---|
| Repo | skeleton, vendored ArduinoCore-API, SDK submodule, `package.json`, `boards.txt`, `platform.txt` |
| PlatformIO | `platform-ch32v` branch, `framework-arduinoch32h4` package, board JSON, builder script |
| Boot | V3F stub and V5F entry in one ELF; VIO18; clocks with HSE/HSI fallback and announcement |
| Linker | one script, both images, `--print-memory-usage`, the §4.4 constraints, alignment assert |
| Faults | handler with its **own** raw UART bring-up |
| Time | `millis`, `micros`, `delay`, `delayMicroseconds` — SysTick at HCLK |
| GPIO | `pinMode`, `digitalWrite`, `digitalRead`, `digitalPinToInterrupt` |
| Interrupts | `attachInterrupt`, `detachInterrupt` |
| Analog | `analogRead`, `analogWrite`, `analogReadResolution`, `analogWriteResolution` |
| Serial | `Serial` on USART1, PA9 TX / PA10 RX, AF7, 115200 8N1 |
| API | ArduinoCore-API: `String`, `Print`, `Stream`, `IPAddress`, `Client`/`Server`/`UDP` |
| Build | exceptions menu, both settings link and run |
| Bench | XIP vs RAM-resident, per §4.3 |

Out of scope for M1: USB, the second core, Wire, SPI, EEPROM, networking.

### 7.2 Three defences, built in at M1

`-Wall` catches none of §2.5, so these are structural rather than advisory.

1. **A single clock-enable helper that takes the bus and the peripheral
   together**, and reads the register back afterwards. The bus cannot be
   mismatched at a call site, and the first-access-dropped bug cannot recur.
   That bug hit four separate drivers in the libhal port before it was
   centralised.
2. **Block reset at init**, through one helper carrying the §2.5 exception
   list, so no driver has to remember it.
3. **A pin/AF configuration helper that sets the mode register and the AF mux
   together**, and enables the AFIO clock first. Either half alone is a
   silent failure.

### 7.3 EXTI

EXTI lines are shared by pin *number* across ports: PA0 and PB0 cannot both
have an interrupt. `attachInterrupt` **refuses** the second and reports it,
rather than silently stealing the line from the first.

### 7.4 Testing

pytest over the console serial port, every test resetting the board, modelled
on the two existing suites.

- **A boot check before any test runs**, which ends the session on failure.
  Without it a dead board costs every test its full timeout, and a suite that
  runs in a minute takes ten to report that the firmware does not boot.
- **Flash verification that distinguishes tool failure from firmware failure.**
  `wlink` prints `Flash done` even when a `0x55` protocol error has voided the
  write. Both are asserted, and a tool failure exits with a **distinct** status
  so it can never be read as a firmware result. This was the single most
  expensive mistake in the libhal port: a wedged probe is indistinguishable
  from firmware that does not boot, and produced a dozen confident false
  reproductions of a bug that did not exist.
- **Binary comparison instead of re-running hardware**, via `size -A` and an
  `nm --print-size` symbol diff, to answer "did this change anything" in
  seconds.
- `wlink` pinned to **0.1.2, the x86 build**, on Windows. 0.1.1 reports "Probe
  is not attached to an MCU" on this part; 0.1.2 x64 fails with a driver error
  on the same machine and probe.
- Preconditions are detected and reported as preconditions, so a missing
  jumper skips rather than fails.

### 7.5 Done when

Blink and a Serial echo run on the board; the suite is green with exceptions
both enabled and disabled; `--print-memory-usage` reports the §4.2 layout; and
the §4.3 benchmark has a number in it.

## 8. Risks

| Risk | Mitigation |
|---|---|
| Single-ELF dual-image has no prior art on this part | The V3F stub and a bare V5F payload are brought up and proven *before* any Arduino API work, so a wake failure is never confused with a core-API failure. This is the staging both prior ports used. |
| XIP too slow (§4.3) | Measured in M1; fallback is a `RAM_CODE` region carved from heap, no interface change |
| A fault during static init is silent | Console up before static constructors; fault handler brings up its own UART |
| Exceptions link but fail at run time | `__register_frame_info` from a priority-101 ctor; `nano.specs` banned; a test that actually throws and catches |
| Vendor SDK drivers that are wrong but compile | §7.2 helpers; `ADCPRE` written by hand; register read-back |
| A wedged `wlink` read as a firmware failure | §7.4 distinct exit status, and a known-good control image |
