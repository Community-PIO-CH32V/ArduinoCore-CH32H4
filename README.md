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

`setup()` and `loop()` run on the V5F. `setup1()` and `loop1()` will run on the
V3F, as in [arduino-pico](https://github.com/earlephilhower/arduino-pico) — see
the milestones below.

## Memory

| Region | Base | Size | Use |
|---|---|---|---|
| ITCM | `0x200A0000` | 128 KB | measured-hot code only, via `__itcm_func` |
| DTCM | `0x200C0000` | 256 KB | `.data`, `.bss`, stack, fast heap |
| Shared SRAM | `0x20100000` | 512 KB | DMA buffers, bulk heap |
| Flash | `0x08000000` | 960 KB | 32 KB V3F stub, 912 KB image, 16 KB EEPROM |

Code runs XIP from flash behind the V5F's I-cache. About **660 KB of heap** is
available to sketches. `_sbrk` hands out DTCM first — zero-wait at 400 MHz —
then continues into the shared region.

## Status

| | Milestone | State |
|---|---|---|
| **M1** | Boot, clocks, single ELF, core Arduino API | in progress |
| M2 | TinyUSB, `Serial` as USB CDC, Adafruit_TinyUSB | planned |
| M3 | Wire, SPI, EEPROM, Servo, Tone, I2S, ADCInput, Ticker | planned |
| M4 | `setup1()`/`loop1()` on the V3F, IPC FIFO, HSEM mutexes | planned |
| M5 | SD, SDFS, FatFS | planned |
| M6 | lwIP, on-chip Ethernet, the `lwip_*` library structure | planned |
| M7 | mbedTLS with the ECDC AES and TRNG accelerators | planned |

## Building

```sh
git clone --recursive https://github.com/Community-PIO-CH32V/arduino-core-ch32h4.git
```

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

C++ exceptions are a build option, off by default:

```ini
board_build.exceptions = enabled
```

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
