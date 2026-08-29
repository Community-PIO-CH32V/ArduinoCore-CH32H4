# Memory baseline

Measured from `tests/sketches/minimal`, which is an empty `setup()`/`loop()`.
Every later task compares against this; a surprise here is a regression.

Regenerate with `python -m pytest tests/test_linker.py -v`, which asserts the
region boundaries, or read the `Memory Configuration` block of
`.pio/build/ch32h417/firmware.map`.

## Regions as linked

```
Name             Origin       Length     Purpose
FLASH_V3F        0x00000000   32 K       V3F stub
FLASH_V5F        0x00008000   912 K      Arduino image (stops at the EEPROM base)
ITCM             0x200A0000   128 K      .itcm_text, measured-hot code only
RAM_LOAD         0x200C0000   256 B      the .load copy stub
DTCM             0x200C0100   255.75 K   .data, .bss, both stacks, fast heap
USB_RAM          0x20100000   8 K        reserved, M2
ETH_RAM          0x20102000   28 K       reserved, M6
SD_RAM           0x20109000   8 K        reserved, M5
XCORE_RAM        0x2010B000   8 K        reserved, M4
SHARED           0x2010D000   460 K      bulk heap
```

## Heap, empty sketch

| | Bytes | Range |
|---|---|---|
| DTCM half | 243,408 | `0x200C4930`–`0x20100000` |
| Shared half | 471,040 | `0x2010D000`–`0x20180000` |
| **Total** | **714,448** (697.7 KB) | |

That is above the spec's ~660 KB estimate, because the estimate assumed a
larger `.data`/`.bss` than an empty sketch has. It will fall as the core grows;
the number to watch is the DTCM half, since `.data`, `.bss` and both stacks all
come out of it before the heap starts.

`_v5f_entry` links at `0x00008000`, which matches `CH32_V5F_START_ADDR` in
`tools/platformio-build.py`. The linker script asserts the 1 KB alignment that
`NVIC_WakeUp_V5F` silently requires, and `main_v3f.c` asserts the two agree.
