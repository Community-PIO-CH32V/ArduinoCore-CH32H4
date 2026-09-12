# CH32H41x package variants: design

**Status:** approved 2026-09-11.

## Goal

Support every part in the CH32H41x family in this core, not just the
CH32H417QEU6 on the desk, and make the PlatformIO board definitions in
`platform-ch32v` build Arduino sketches for each of them.

Five parts:

| Part | Package | Pin table |
|------|---------|-----------|
| CH32H417QEU6 | QFN128 | Table 2-1-1, column H417QEU6 |
| CH32H417MEU6 | QFN88 | Table 2-1-1, column H417MEU6 |
| CH32H417WEU6 | QFN68 | Table 2-1-1, column H417WEU6 |
| CH32H416RDU6 | QFN60 | Table 2-1-2 |
| CH32H415REU6 | QFN60 | Table 2-1-3 |

## Data sources

**The datasheet is authoritative.** `CH32H417DS0-2.PDF`, V1.5, section 2.2
"Pin Description". Three separate tables, because the three part numbers are
not the same silicon. Table 2-1-1 covers the H417 packages with one pin-number
column each; 2-1-2 and 2-1-3 have a single column each. A `-` in a column means
the pad is not bonded on that package.

The alternate-function lists differ per table, not only per package. `PE3` on
H417 offers `SERDES_TXP`; on H415 it does not. So each part's map is extracted
from its own table rather than filtered from the QEU6 map.

**The SDK is the independent check.** `GPIO_IPD_Unused()` in
`system/ch32h417lib/Peripheral/src/ch32h417_gpio.c` ties unbonded pads to a
defined level, with a case per package keyed on the word at `0x1FFFF704`. Its
pin lists are therefore the complement of the datasheet's bonded columns, and
the two must agree exactly. They already agree on a spot check: the datasheet
shows `PE2` as `-` for MEU6, and the SDK lists `GPIO_Pin_2` for `GPIOE` in the
MEU case.

There is no case for QEU6 because QFN128 bonds every pad, so for that part the
check is that the bonded set is all 95.

**Do not use the block diagrams.** Figures 1-1-2 and 1-1-3 are both captioned
"CH32H416 system block diagram", so one of them is mislabelled and neither can
be trusted to say what a part contains. Peripheral presence is derived from the
pin tables instead: a block whose alternate functions appear on no pin of a
part is not reachable on that part, whatever the diagram claims.

## Decisions

1. **All five parts** in this pass.
2. **Pin numbers stay `port * 16 + bit`** on every part. `PB3` is 19 everywhere.
   Unbonded pads are absent from the pin map, so using one fails at runtime, not
   at compile time. This keeps the numbering shared with the MicroPython port
   and keeps a sketch portable across packages.
3. **Alternate-function maps are extracted per part** from that part's own
   table.
4. **Acceptance** is the SDK cross-check, a compile of every variant, and the
   44 existing hardware tests still passing on the QEU6 board. No hardware
   claim is made for the other four parts.

## Architecture

### The generator

`tools/genvariants.py`, run by hand, not by the build:

```
python tools/genvariants.py --datasheet C:\path\CH32H417DS0-2.PDF
```

The datasheet is not redistributable and must not enter the repo, and a build
must never depend on a file in someone's Downloads folder. So the generator is
committed, its output is committed, and the PDF is supplied on the command
line.

Extraction is by coordinate band, not by text order. The table has no ruling
lines that `find_tables()` can see, but the columns sit at stable x positions
(25.7, 49.5, 73.2 for the three H417 columns, pin name near 107). Words are
grouped into rows by y and assigned to columns by x.

For each part the generator writes:

- `variants/<BASE>/pins_package.h` — pin names, counts, ADC and DAC pins,
  supply-domain predicate, peripheral presence macros.
- `variants/<BASE>/pin_map_package.c` — the pin and alternate-function tables.
- `variants/<BASE>/pinout.json` — the same data, machine readable, for the test
  to check without needing the PDF.
- `variants/<BASE>/peripherals_package.h` — per-peripheral default pins, only
  for peripherals the part has.

Package base directory names follow the existing one, `CH32H417QEU6`:

```
CH32H417QEU6   CH32H417MEU6   CH32H417WEU6
CH32H416RDU6   CH32H415REU6
```

### What changes per part, beyond which pins exist

**Supply domains.** H416 and H415 have no `VIO18` or `VDDIO` rail at all, so
`PIN_IS_3V3_DOMAIN` cannot be copied from the QEU6 base. It is generated from
each table's own pin-type column.

**Peripheral presence.** Each base defines a macro per block it has, for
example `CH32H4_HAS_ETH`, `CH32H4_HAS_FSMC`, `CH32H4_HAS_SDIO`,
`CH32H4_HAS_SERDES`, `CH32H4_HAS_QSPI1`. A library for hardware the part lacks
fails with `#error` naming the part, rather than with a missing register.

**Peripheral defaults.** `peripherals_package.h` keeps its `#ifndef` guards so
a board can override any default, and omits blocks the part does not have.

### Board variants versus package bases

The existing split is already right and does not change: a package base holds
what the silicon decides, and a board variant holds what one PCB wired. The
board variant for the evaluation board is renamed to make the distinction
legible, since `variants/CH32H417QEU6` currently reads like a package:

```
variants/CH32H417QEU6  ->  variants/CH32H417QEU6_EVT_R0
```

Its `package.txt` and single `#include` are the only things that point at the
base, so the rename touches `boards.txt`, the board JSON in `platform-ch32v`,
and nothing else.

No board variants are invented for the other four parts. A package base is
directly usable as a variant for a generic board, which is what the generic
board definitions want.

## Board definitions in platform-ch32v

The generic boards already exist but cannot build Arduino sketches: they carry
no `build.core`, no `build.variant`, and do not list `arduino` among their
frameworks. Each gains those three things.

Two are wrong and are fixed here. `genericCH32H417REU6*.json` names a part that
does not exist, since REU6 is a CH32H415 part, so those three files are
replaced by `genericCH32H415REU6*.json`. There is no CH32H416RDU6 board at all,
so `genericCH32H416RDU6*.json` is added.

`ch32h417qeu6_evt_r0.json` keeps its name and URL and gains the renamed
variant. It already defaults to the Arduino framework and the `ch32h4` core, so
"use the new core by default" is already true for it; the change is the variant
name only.

## Testing

**`tests/test_variants.py`, no hardware.** For each part, load
`variants/<BASE>/pinout.json` and assert:

- the bonded set and the SDK's `GPIO_IPD_Unused()` complement are equal, parsed
  out of the SDK source so the check follows the vendor's file rather than a
  copy of it;
- every bonded pin's number equals `port * 16 + bit`;
- no pin is named that the die does not have, which means nothing above `PF14`;
- every alternate function in the map names a peripheral the part declares.

**`tests/test_link_matrix.py`.** One compile per variant, so a generated table
that does not build is caught.

**Hardware.** The 44 existing tests on the QEU6 board, unchanged. They are the
only evidence any of this runs, and they cover exactly one of the five parts.

## Out of scope

- Board variants for boards that do not exist.
- Any claim that the other four parts work on hardware.
- The V3F and V5F generic board pairs beyond giving them the variant and core;
  their clock and core settings are left as they are.
- `tool-wlink` and the flashing path, done separately.
