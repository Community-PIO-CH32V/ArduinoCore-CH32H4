# The V5F instruction cache

The CH32H417's application core, the QingKe V5F, has a 32 KB instruction
cache. Flash is clocked at HCLK/2 = 50 MHz while the core runs at 400 MHz, so
uncached execution from flash pays that gap on every fetch. Turning the cache
on is worth a measured **145x** for flash-resident code.

The V3F, the core that boots first, has **no instruction cache at all**. Every
statement below is about the V5F only.

## What it costs to get wrong

Same loop, compiled twice, one copy in flash and one in ITCM:

| | XIP (flash) | ITCM | ratio |
|---|---|---|---|
| cache off, the reset default | 365,606 us | 2,517 us | 145x |
| cache on | 2,507 us | 2,505 us | 1.00x |

Nothing reports the difference. The board boots, enumerates USB, gets a DHCP
lease and passes every functional test either way. Only a cycle count tells
you, which is why both mistakes below survived for a while.

## How it is initialised

In `cores/ch32h4/startup_v5f.S`, before jumping to `ch32h4_v5f_main`. The
sequence is WCH's, from `EVT/EXAM/CPU/ICache`, and the register values live in
`cores/ch32h4/ch32h4_csr.h`.

```asm
la   t0, _cache_beg        /* PMP channel 1, TOR: base  */
srli t0, t0, 2             /* addresses are stored >> 2 */
csrw pmpaddr0, t0
la   t0, _cache_end        /* and top                   */
srli t0, t0, 2
csrw pmpaddr1, t0

li   t0, CACHE_PMP1_STRTG  /* 0xBC3 cache_pmp_ovr: policy applies to channel 1 */
csrw 0xbc3, t0
li   t0, PMPCFG0_V5F       /* 0x3A0 pmpcfg0: the channel-1 byte, IC_Str set    */
csrw 0x3a0, t0
li   t0, OPCACHE_INVAL     /* 0xBD0 opcache_ctlr: invalidate before enabling   */
csrw 0xbd0, t0
li   t0, CACHE_V5F_CSRC    /* 0xBC2 cache_strtg_ctlr: CLEAR three bits         */
csrc 0xbc2, t0
```

Order matters: the region is described first, the cache is invalidated, and
only then is it enabled. The last instruction is a `csrc`, a clear, not a
write.

## Bit arrangement

**`cache_strtg_ctlr`, CSR `0xBC2`.** Three bits, and all three **reset to 1**.

| bit | name | meaning | reset |
|---|---|---|---|
| 1 | `ic_disable` | 1 = cache off | 1 |
| 24 | `ic_code_strtg` | blanket: cache all of `0x00000000-0x1fffffff` | 1 |
| 25 | `ic_sram_strtg` | blanket: cache all of `0x20000000-0x3fffffff` | 1 |

The core clears all three together, which is `CACHE_V5F_CSRC` = `0x03000002`.
Clearing only `ic_disable` is the obvious reading of the manual and it does not
work: the two blanket enables then make the cache cover the whole of flash and
the whole of the TCM and SRAM window, including the ITCM the vector table and
hot code live in, and the core traps during startup. With the blanket bits
clear, what gets cached is decided by the PMP entry instead.

**`cache_pmp_ovr`, CSR `0xBC3`.** Bit 4, `pmp1_strtg`: apply the cache policy
to PMP channel 1.

**`opcache_ctlr`, CSR `0xBD0`.** Bits `[1:0]` select the operation, `00` being
invalidate; bit 2 says the value in `vaddr` is an address. `OPCACHE_INVAL` is
`0b100`.

**`pmpcfg0`, CSR `0x3A0`.** Channel 1 occupies byte 1, so the composed value is
shifted left by 8. The channel-1 byte is `R | X | A=TOR | IC_Str | L`, which
is `0xAD`, giving `0xAD00`.

| bit | name | meaning |
|---|---|---|
| 0 | `R` | readable |
| 2 | `X` | executable |
| 4:3 | `A` | `01` = top-of-range, base `pmpaddr0`, top `pmpaddr1` |
| **5** | **`IC_Str`** | **apply the cache policy to this region** |
| 7 | `L` | lock, enforced in M-mode |

### IC_Str is bit 5, not bit 6

Table 4-3 of the QingKeV5 Microprocessor Manual puts `IC_Str` at **bit 6** and
marks bit 5 reserved. WCH's own example writes `0xAD00`, which sets **bit 5**.
The silicon follows the example, not the manual.

| `pmpcfg0` | flash | ITCM | ratio |
|---|---|---|---|
| `0xAD00`, bit 5 | 140,491 cyc | 160,908 cyc | 0.87x |
| `0xCD00`, bit 6 | 20,095,779 cyc | 166,976 cyc | 143x |

Both values are accepted and read back unchanged, so no register inspection
distinguishes a cache that is covering the region from one that is not. This is
the one place in `ch32h4_csr.h` that deliberately contradicts the manual.

## What is cached

Exactly the flash-resident code and read-only data, bracketed in the linker
script:

```
.text   { _cache_beg = . ; ... }
.rodata { ... ; _cache_end = . }
```

ITCM is deliberately **outside** the window: it is already zero-wait, so
caching it would add nothing and cost a region. The filesystem partition and
the EEPROM live in the flash tail, above `_cache_end`, so they are outside too
— which is why writing them needs no cache maintenance.

## Pitfalls

**It is off at reset, and silence is the symptom.** Nothing warns, nothing
fails, and every test passes. If flash-resident code is inexplicably slow,
check the cache before anything else, and check it by timing rather than by
reading registers back.

**Next-line branch prediction must stay off.** `CORECFGR` bit 15 `NLP_EN` makes
this core resume execution at an address it was never sent to, but **only when
the cache is on**:

| | result |
|---|---|
| cache on, NLP on | faults, deterministically |
| cache off, NLP on | runs, since NLP does nothing without the cache |
| cache on, NLP off | runs, at full cached speed |

The failure looks like a wild pointer in whatever library happens to be
running, with a register state no legitimate call could produce, and it moves
with code layout, so any unrelated change hides it. WCH's startup sets this
bit; this core does not. `-DCH32H4_V5F_NLP` puts it back for anyone wanting to
re-measure.

**There is exactly one invalidate, in startup.** Nothing invalidates at
runtime, and nothing needs to, because no path writes to the cached window. The
filesystem, the EEPROM and an OTA image all live above `_cache_end`, and an OTA
image is only executed after a reset, which invalidates anyway. **If you add a
path that writes flash inside `_cache_beg.._cache_end` — self-modifying code, a
patch loader — you must invalidate through `0xBD0` yourself.** The cache will
otherwise keep serving the old instructions, and the code you just wrote will
appear not to have been written.

**Both cores execute XIP from the same array.** The V5F survives programming
its own flash partly because its loop runs from cache while the array is busy.
The V3F has no cache and fetches every instruction from the array being
written, so it must be parked in ITCM during a flash operation. See the
dual-core flash section in `hazards.md`.

## Where to look

| file | what it holds |
|---|---|
| `cores/ch32h4/ch32h4_csr.h` | every bit definition, with the measurement behind it |
| `cores/ch32h4/startup_v5f.S` | the enable sequence, and the NLP reasoning |
| `variants/*/ch32h417.ld` | `_cache_beg` and `_cache_end` |
| `docs/hazards.md` | the two hazards above, at length |
