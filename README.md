# Arduino core for the CH32H41x

An Arduino core for the WCH CH32H417, implementing the
[ArduinoCore-API](https://github.com/arduino/ArduinoCore-API) and building under
PlatformIO. Baremetal on the WCH SDK, no RTOS.

Board: `CH32H417QEU6-R0-1v1` (chip `CH32H417QEU8`).

## The shape of it

This part has two RISC-V cores. The **V3F** (100 MHz, in-order) boots first; the
**V5F** (400 MHz, out-of-order, 32 KB I-cache) is where your sketch runs. The
V3F configures the clock tree, brings up a console, wakes the V5F and parks in
Stop mode.

Both cores live in **one ELF**. Both prior ports to this silicon ship two images
merged by a script, because WCH's stock startup files for the two cores define
the same ~150 symbols and collide at link time. This core writes its own
startup with prefixed symbols, so one link produces one `.bin`.

`setup()` and `loop()` run on the V5F. `setup1()` and `loop1()` run on the V3F,
as in [arduino-pico](https://github.com/earlephilhower/arduino-pico) — though
the assignment is the other way round there, because here the boot core is the
slow one and a sketch that says nothing about cores should get the fast one.

Define neither and the V3F sleeps, which costs a single-core sketch nothing.
`CH32H4.fifo` carries words between the cores and `CH32H4.mutexLock()` wraps the
hardware semaphores.

## Memory

| Region | Base | Size | Use |
|---|---|---|---|
| ITCM | `0x200A0000` | 128 KB | measured-hot code only, via `__itcm_func` |
| DTCM | `0x200C0000` | 256 KB | `.data`, `.bss`, stack, fast heap |
| Shared SRAM | `0x20100000` | 512 KB | DMA buffers, bulk heap |
| Flash | `0x08000000` | 960 KB | 32 KB V3F stub, 912 KB image, 16 KB EEPROM |

Code runs XIP from flash, behind the V5F's 32 KB instruction cache — which is
disabled at reset and which this core enables, worth a measured 145x. About
**708 KB of heap** is available to sketches:
`_sbrk` hands out DTCM first — zero-wait at 400 MHz — then continues into the
shared region, and newlib's allocator opens a second segment at the gap.

ITCM holds the V5F's trap vector table, which has to be in RAM for correctness
rather than speed, plus whatever is marked `__itcm_func` — currently
`digitalWrite`, `digitalRead`, `millis`, `micros`, `delayMicroseconds` and the
EXTI dispatch. See "Known gaps" for why that placement still matters.

## Status

All seven milestones are implemented, and everything below marked *verified*
has been exercised on a CH32H417QEU6 with the SD card, Ethernet, an I2S
amplifier and a WCH-Link attached.

| | Milestone | State |
|---|---|---|
| **M1** | Boot, clocks, single ELF, core Arduino API | verified |
| **M2** | Adafruit TinyUSB, `Serial` as USB CDC | verified |
| **M3** | Wire, SPI, EEPROM, Servo, Tone, I2S, ADCInput, DAC | verified — see the gaps below |
| **M4** | `setup1()`/`loop1()` on the V3F, FIFO, HSEM mutexes | verified |
| **M5** | SD block layer, FatFs R0.16, the FS/File API, the classic `SD` shim | verified |
| **M6** | lwIP 2.2.1, on-chip Ethernet, TCP/UDP client and server, SNTP | verified |
| **M7** | mbedTLS 3.6.7, AES on the ECDC block, entropy from the TRNG | verified |
| | RTC on LSI, LSE and HSE, wired into `gettimeofday()` | verified |

`python -m pytest tests/hw` runs **152 hardware tests** against a connected
board, and `python -m pytest tests/link` a 51-case matrix of build
configurations. The suite reprograms the part eight times — once per sketch —
and takes about three minutes.

### What is not verified

Three things, all of them limited by the bench rather than by the code:

* **I2S receive** and **slave mode**. Receive needs a microphone and slave mode
  needs an external clock, and neither is attached. The transmit path is
  verified on both I2S blocks; the receive half-word ordering is carried from
  the MicroPython driver's measurements rather than re-measured here.
* **Whether the I2S output sounds right.** The tests confirm the divider, the
  DMA throughput and the underflow count, all of which are independent of the
  sample values — so the whole suite runs on silence. Judging the audio needs
  ears or a scope.
* **The ADC's and DAC's absolute accuracy.** Rate, throughput, channel
  ordering, the internal reference and the die temperature are all checked, and
  the DAC is checked end to end by reading its own pad back through the ADC —
  PA4 is ADC4 and PA5 is ADC5, so that needs nothing wired. But both converters
  carry their own error, so this is not a linearity measurement, and there is
  no calibrated source on the bench to make one against.

Everything else that was once a gap is now covered by a test, including the
negative cases that matter most — a TLS client that accepts a certificate from
the wrong CA passes every positive test there is.

## Building

```sh
git clone --recursive https://github.com/Community-PIO-CH32V/arduino-core-ch32h4.git
```

### One thing to know about the TinyUSB submodule

`libraries/Adafruit_TinyUSB_Arduino` is checked out on a **fork branch**
(`ch32h417`) that upstream Adafruit does not have. Upstream bundles TinyUSB
0.20, which has neither `OPT_MCU_CH32H417` nor the USBFS device driver for this
part, so the library cannot build for this silicon at all. The fork swaps in
0.21 from [maxgerhardt/tinyusb](https://github.com/maxgerhardt/tinyusb)
(branch `ch32h417-v2`) and adds an `ARDUINO_ARCH_CH32H4` branch to its
`tusb_config.h` dispatcher.

That branch still needs pushing to a fork, and `.gitmodules` updating to point
at it. Until then a fresh `git submodule update --init` checks out upstream and
the build stops with *"TinyUSB Arduino Library does not support your core
yet"* — which is the dispatcher saying exactly this.

`--recursive` matters: `system/ch32h417lib` carries the vendor SDK and
`ArduinoCore-API` the upstream API. `python tools/check_tree.py` says so plainly
if either is missing.

Then point a PlatformIO project at it:

```ini
[env:ch32h417]
platform = https://github.com/Community-PIO-CH32V/platform-ch32v.git#feature/ch32h4-arduino
board = ch32h417qeu6_evt_r0
framework = arduino
```

`Serial` is USB CDC by default and `Serial1` is USART1 on PA9/PA10. To swap
them — worth doing while debugging anything that can fault before USB
enumerates:

```ini
board_build.serial = uart
```

C++ exceptions are a build option, off by default:

```ini
board_build.exceptions = enabled
```

Both settings build, link and run; a `throw` is caught on hardware. Enabling
costs roughly 30 KB of unwind tables, which stay in flash.

## Testing

```sh
python -m pytest tests/ -v            # host-side: tree, linker, build
python -m pytest tests/hw/ -v         # on hardware, over the console
```

The hardware suite flashes the board itself and needs the WCH-Link's VCP;
override the port with `CH32_PORT`. It checks the board boots once, before any
test runs, and ends the session if it does not — otherwise a dead board costs
every test its full timeout.

## Notes for anyone working on this

Most of this silicon's failures produce no error at all: a wrong RCC bus reads
back as zeroes, an alternate-function pin without its mux runs perfectly and
puts nothing on the wire, erased flash reads `0xE339E339` rather than `0xFF`,
and `--specs=nano.specs` makes every `throw` reach `std::terminate` after a
clean link. `docs/superpowers/specs/` records them and the design that routes
around them; the prior ports' `PORTING.md` and `STATUS.md` are the deeper
source, with three corrections noted in the spec.
