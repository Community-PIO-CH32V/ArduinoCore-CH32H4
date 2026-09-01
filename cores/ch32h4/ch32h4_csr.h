/* CSR bit flags for the QingKeV5, from the QingKeV5 Microprocessor Manual (V1.0).
 *
 * Each flag names a bit or field and cites the manual section that defines it,
 * so the two startup files (startup_v3f.S, startup_v5f.S) can compose their
 * per-core register values from named parts instead of magic constants.
 *
 * Where the manual marks a bit "Reserved" or leaves it undocumented, the value
 * is WCH's own startup value, reproduced verbatim under a WCH-prefixed name
 * rather than guessed. Those bits are only ever cleared/shaped deliberately by
 * the core that owns them, so a name that says WCH is a warning to leave it.
 */
#ifndef CH32H4_CSR_H
#define CH32H4_CSR_H

/* ---- mstatus (0x300), manual §3.2 (gintenr table) ----
 * FS/MPP/MPIE/MIE are the standard RISC-V fields; the manual §3.2 gintenr table
 * lists the same layout. MPP must be 3 (Machine): WCH ships 0x6088, whose
 * MPP=00 drops the core into User mode on mret, where every M-mode CSR becomes
 * unreachable and the board dies silently (docs/hazards.md). */
#define MSTATUS_MIE           (1 << 3)    /* [3]     global interrupt enable */
#define MSTATUS_MPIE          (1 << 7)    /* [7]     MIE saved on trap entry */
#define MSTATUS_MPP_M         (3 << 11)   /* [12:11] = 3 -> trap returns to Machine */
#define MSTATUS_FS_DIRTY      (3 << 13)   /* [14:13] = 3 -> FPU on (Dirty) */
#define MSTATUS_RUN           (MSTATUS_FS_DIRTY | MSTATUS_MPP_M | MSTATUS_MPIE | MSTATUS_MIE)

/* ---- mtvec (0x305), manual §3.2 ---- */
#define MTVEC_MODE0           (1 << 0)    /* [0] 1 = entry offset by irq number * 4 */
#define MTVEC_MODE1           (1 << 1)    /* [1] 1 = absolute addresses in the table */
#define MTVEC_MODE_VECT       (MTVEC_MODE0 | MTVEC_MODE1)

/* ---- intsyscr (0x804), manual §3.2 ---- */
#define INTSYSCR_HWSTKEN      (1 << 0)    /* [0]  hardware stack (HPE) on */
#define INTSYSCR_INESTEN      (1 << 1)    /* [1]  interrupt nesting on */
#define INTSYSCR_PMTCFG_1     (1 << 2)    /* [3:2] = 01 -> 1 preemption bit */
#define INTSYSCR_PMTCFG_3     (3 << 2)    /* [3:2] = 11 -> 3 preemption bits */

/* ---- inestcr (0xBC1), manual §3.2 ---- */
#define INESTCR_NEST_LVL_2    1           /* [2:0] = 001 -> allow 2-level nesting */
#define INESTCR_NEST_LVL_8    7           /* [2:0] = 111 -> allow 8-level nesting */

/* ---- corecfgr (0xBC0), manual §3.2 ----
 * The FPU clock dividers are the manual's reset defaults. CSTA_FAULT_IE (bit 7)
 * turns a core status error (a bus error) into an NMI; IE_REMAP_EN (bit 5)
 * maps MIE/MPIE into the user-mode gintenr register. The WCH_* bits are the
 * values WCH's own startup leaves in place and are not to be reasoned about. */
#define CORECFGR_FADD_DIV     (1 << 28)   /* [31:28] FADD clock divider (default 1) */
#define CORECFGR_FMUL_DIV     (2 << 24)   /* [27:24] FMUL clock divider (default 2) */
#define CORECFGR_FMAC_DIV     (3 << 20)   /* [23:20] FMAC clock divider (default 3) */
#define CORECFGR_FDIV_DIV     (7 << 16)   /* [19:16] FDIV clock divider (default 7) */
#define CORECFGR_NLP_EN       (1 << 15)   /* [15]    next-line branch prediction */
#define CORECFGR_WCH_13_12    (3 << 12)   /* [13:12] undocumented, WCH = 11b */
#define CORECFGR_WCH_9_8      (3 << 8)    /* [9:8]   reserved, WCH = 11b */
#define CORECFGR_CSTA_FAULT_IE (1 << 7)   /* [7]     NMI on core status error */
#define CORECFGR_WCH_6        (1 << 6)    /* [6]     reserved, WCH = 1 */
#define CORECFGR_IE_REMAP_EN  (1 << 5)    /* [5]     map MIE/MPIE into gintenr */
#define CORECFGR_WCH_0        (1 << 0)    /* [0]     reserved, WCH = 1 (V3F only) */

/* ---- hw_popdm_addr (0xBC4), manual §8 ----
 * The hardware interrupt stack's base. DELIBERATELY NOT WRITTEN.
 *
 * The manual gives the reset value as 0x200A0000 and this silicon reports
 * 0x200C0000 -- the DTCM base, which is what the register's own text ("points
 * to the DTCM area") calls for and what WCH's V5F linker script reserves 512
 * bytes at. Read it back before trusting either.
 *
 * Writing it moved the interrupt stack into ITCM, on the strength of the
 * manual's number, and nothing failed loudly. The 512 bytes are reserved in
 * the linker script (HW_STACK) instead; see the comment there. */

/* ---- cache_strtg_ctlr (0xBC2), manual §8 ----
 * ic_disable (bit 1) AND the two blanket enables (bits 24/25) reset to 1 and
 * must be cleared together: leaving 24/25 set makes the cache cover all of
 * flash and all of SRAM -- including ITCM -- and the core traps in startup.
 * Caching is then scoped by the PMP entry instead. */
#define CACHE_IC_DISABLE      (1 << 1)    /* [1]  1 = cache off */
#define CACHE_IC_CODE         (1 << 24)   /* [24] cache 0x00000000-0x1fffffff */
#define CACHE_IC_SRAM         (1 << 25)   /* [25] cache 0x20000000-0x3fffffff */
#define CACHE_V5F_CSRC        (CACHE_IC_SRAM | CACHE_IC_CODE | CACHE_IC_DISABLE)

/* ---- cache_pmp_ovr (0xBC3), manual §8 ---- */
#define CACHE_PMP1_STRTG      (1 << 4)    /* [4] 1 = cache PMP channel 1 region */

/* ---- opcache_ctlr (0xBD0), manual §8 ---- */
#define OPCACHE_OP_INVAL      0           /* [1:0] = 00 -> I-cache invalidate */
#define OPCACHE_IDX_ADDR      1           /* [2]   1 = vaddr is an address */
#define OPCACHE_INVAL         ((OPCACHE_IDX_ADDR << 2) | OPCACHE_OP_INVAL)

/* ---- pmpcfg0 (0x3A0), manual §4.2 ----
 * Channel 1 (byte 1) both scopes the I-cache and protects the flash code+rodata
 * as read+execute. IC_STR is what makes the cache_pmp_ovr policy apply to this
 * region; without it the region runs uncached. A=01 is TOR, taking pmpaddr0 as
 * base and pmpaddr1 as top. */
#define PMPCFG_R              (1 << 0)    /* [0] readable */
#define PMPCFG_X              (1 << 2)    /* [2] executable */
#define PMPCFG_A_TOR          (1 << 3)    /* [4:3] = 01 -> top-of-range */
/* IC_Str: bit 5, NOT the bit 6 the manual's table 4-3 gives.
 *
 * Measured. With bit 5 (WCH's own EVT/EXAM/CPU/ICache value) a flash-resident
 * benchmark runs in 140k cycles against ITCM's 161k -- 0.87x, cache on. With
 * bit 6 the same loop takes 20.1M cycles -- 143x, cache off. Both values are
 * accepted by pmpcfg0 and read back unchanged, so the register tells you
 * nothing; only the cycle count does.
 *
 * This is the one bit in this file that contradicts the manual. It is here
 * because turning the instruction cache off costs two orders of magnitude and
 * produces no error, no warning and no failing test. */
#define PMPCFG_IC_STR         (1 << 5)
#define PMPCFG_L              (1 << 7)    /* [7] lock (enforced in M-mode) */
#define PMPCFG_CH1            (PMPCFG_L | PMPCFG_IC_STR | PMPCFG_A_TOR | PMPCFG_X | PMPCFG_R)
#define PMPCFG0_V5F           (PMPCFG_CH1 << 8)   /* channel 1 sits in byte 1 */

#endif /* CH32H4_CSR_H */
