/* V3F startup -- the first code on the chip.
 *
 * Every symbol here is prefixed. WCH's stock startup_ch32h417_v3f.S and
 * startup_ch32h417_v5f.S define the same ~150 names -- _start, handle_reset,
 * _vector_base and every weak IRQ handler -- so linking both collides on all
 * of them. That is the single reason both prior ports to this silicon ship two
 * ELFs and merge them with a script. Prefixing costs nothing and removes it.
 *
 * THIS CORE OWNS THE C RUNTIME DATA INIT. In one ELF the two cores share one
 * .data and one .bss, so whichever runs first must initialise them and the
 * other must not repeat it -- a second .bss zero on the V5F would wipe the
 * clock state this core just established. startup_v5f.S therefore copies
 * nothing; it only sets up its own stack, CSRs and vector table, and runs the
 * static constructors that belong to the sketch.
 *
 * The .data copy is not optional and its absence is silent. The vendor SDK's
 * RCC_GetClocksFreq() indexes six `static __I uint8_t` lookup tables, and __I
 * is `const volatile` -- the volatile stops GCC putting them in .rodata, so
 * they live in .data. Without the copy they are garbage, every clock frequency
 * computed from them is wrong, and USART_Init programs a baud rate nothing can
 * read. The board says nothing at all and reports no error.
 */

    .section .init_v3f, "ax", @progbits
    .global _start_v3f
    .align  2
_start_v3f:
    j handle_reset_v3f

/* The V3F's own vector table. It is linked into RAM and copied there by
 * _load_base_v3f -- see the linker script for why a table in flash locks the
 * core up the first time it traps.
 *
 * Every unused entry points at Stray_IRQ_v3f, NOT at zero. A zero entry in
 * mode 3 means "jump to address 0", and address 0 on this part is _start_v3f:
 * a stray interrupt silently re-ran startup, which on the console is
 * indistinguishable from a watchdog reboot. */
    .section .vector_v3f, "ax", @progbits
    .global _vector_base_v3f
    .align  6
_vector_base_v3f:
    .option norvc
    .word   _start_v3f
    .word   0
    .word   NMI_Handler_v3f
    .word   HardFault_Handler_v3f

    /* Slots 4..17: nothing this core has been given a use for yet. The slot
       index IS the IRQ number, the same as the V5F's table next door, so a
       handler added here has to go in the right place. */
    .rept   14
    .word   Stray_IRQ_v3f
    .endr

    /* 18, 19: the IPC channels. This core parks itself in CH2's handler while
       the other one writes flash -- a page program does not complete while
       this core is fetching from the array being written, and this core has
       no instruction cache to hide behind. See cores/ch32h4/ch32h4_park.c.

       An interrupt rather than a flag loop1() has to notice, because loop1()
       is the sketch's and may never look at anything.

       THIS TABLE IS IN SRAM -- V3F_VECTOR, not flash -- so taking the
       interrupt does not itself need a flash read. That is what makes the
       whole arrangement possible. */
    .word   IPC_CH2_Handler_v3f
    .word   IPC_CH3_Handler_v3f

    /* 20..63. */
    .rept   44
    .word   Stray_IRQ_v3f
    .endr

    .section .text.v3f, "ax", @progbits

    .weak   NMI_Handler_v3f
    .weak   HardFault_Handler_v3f
    .weak   Stray_IRQ_v3f
    /* Weak, so a build with no parking still links: they fall through to the
       stray handler, which records and resets. */
    .weak   IPC_CH2_Handler_v3f
    .weak   IPC_CH3_Handler_v3f
    .align  2
NMI_Handler_v3f:
HardFault_Handler_v3f:
IPC_CH2_Handler_v3f:
IPC_CH3_Handler_v3f:
Stray_IRQ_v3f:
    /* Record and reset. Nothing is printed here: this core may be mid-line on
     * the shared UART, or the V5F may own it, so a print would produce
     * interleaved noise at exactly the moment clarity matters. The record goes
     * to .xcore, which is NOLOAD and survives the reset, and the next boot
     * prints it before waking anything.
     *
     * Reset rather than spin. A core spinning here wedges the WCH-Link with a
     * 0x55 protocol error, and the only way back is holding NRST down through
     * an erase. */
    la t0, ch32h4_fault_log_v3f
    csrr t1, mcause
    sw t1, 4(t0)
    csrr t1, mepc
    sw t1, 8(t0)
    csrr t1, mtval
    sw t1, 12(t0)
    csrr t1, mstatus
    sw t1, 16(t0)
    sw sp, 20(t0)
    /* ra and the top of the stack, in the record's otherwise-unused counter
       fields. A bad PC is only ever half the story -- where it was returning
       to is the other half. */
    sw ra, 24(t0)
    lw t1, 0(sp)
    sw t1, 28(t0)
    lw t1, 4(sp)
    sw t1, 32(t0)
    lw t1, 8(sp)
    sw t1, 36(t0)
    lw t1, 12(sp)
    sw t1, 40(t0)
    li t1, 0xF01DFA11          /* CH32H4_FAULT_LOG_MAGIC */
    sw t1, 0(t0)
    /* PFIC->CFGR = KEY3 | SYSRESET. */
    li t0, 0xE000E048
    li t1, 0xBEEF0080
    sw t1, 0(t0)
1:  j 1b

    .global handle_reset_v3f
    .align  2
handle_reset_v3f:
.option push
.option norelax
    la gp, __global_pointer$
.option pop

    la sp, _estack_v3f

    /* Copy the .load routine into RAM and call it there. It reprograms the
       PLL, and changing the PLL while instructions are being fetched from
       flash changes the flash access timing underneath the fetch. This copy
       loop itself is safe in flash because it touches no clocks. */
    la a0, _loadcode_lma
    la a1, _loadcode_vma_start
    la a2, _loadcode_vma_end
    bgeu a1, a2, 2f
1:  lw t0, (a0)
    sw t0, (a1)
    addi a0, a0, 4
    addi a1, a1, 4
    bltu a1, a2, 1b
2:
    call _load_base_v3f

    /* CSR bit flags live in ch32h4_csr.h so both cores share one definition.
     * Only this core's composed register values are spelled out here. This core
     * runs at 100 MHz and handles no sketch interrupts, so it gets one
     * preemption bit, 2-level nesting and the V3F's corecfgr flavour. */
#include "ch32h4_csr.h"

#define CORECFGR_V3F (CORECFGR_FADD_DIV | CORECFGR_FMUL_DIV | CORECFGR_FMAC_DIV | \
                      CORECFGR_FDIV_DIV | CORECFGR_WCH_9_8 | CORECFGR_CSTA_FAULT_IE | \
                      CORECFGR_WCH_6 | CORECFGR_IE_REMAP_EN | CORECFGR_WCH_0)
#define INTSYSCR_V3F  (INTSYSCR_PMTCFG_1 | INTSYSCR_INESTEN | INTSYSCR_HWSTKEN)

    li t0, CORECFGR_V3F
    csrw 0xBC0, t0
    li t0, INESTCR_NEST_LVL_2
    csrw 0xBC1, t0
    li t0, INTSYSCR_V3F
    csrw 0x804, t0

    /* FPU on, interrupts on, and STAY IN MACHINE MODE (see MSTATUS_MPP_M in
     * ch32h4_csr.h for why WCH's 0x6088 here would leave the board silent). */
    li t0, MSTATUS_RUN
    csrw mstatus, t0

    /* Mode 3: vectored, absolute addresses in the table. */
    la t0, _vector_base_v3f
    ori t0, t0, MTVEC_MODE_VECT
    csrw mtvec, t0

    la t0, ch32h4_v3f_main
    csrw mepc, t0
    mret

/* ---------------------------------------------------------------------------
 * Runs from RAM. See the copy above for why.
 */
    .section .load, "ax", @progbits
    .align  2
    .global _load_base_v3f
_load_base_v3f:

    /* Bootstrap PLL: 14 * HSI / 5 = 70 MHz. SystemInit() replaces this with
       the real tree later; this only has to be fast enough and stable enough
       to run the copies below. Sequence taken verbatim from WCH's
       startup_ch32h417_v3f.S, where s1 is the RCC base at 0x40021000. */
    lui s1, 0x40021
    li  a1, 0x40E
    sw  a1, 8(s1)

    /* Enable the PLL. */
    lw  t0, 0(s1)
    li  t1, 0x1000000
    or  t0, t0, t1
    sw  t0, 0(s1)
1:  lw  t2, 0(s1)
    li  t3, 0x2000000
    and t2, t2, t3
    beqz t2, 1b

    /* Enable the PLL gate. */
    lw  t4, 8(s1)
    li  t5, 0x80000000
    or  t4, t4, t5
    sw  t4, 8(s1)

    /* Select the PLL, and wait until it is actually the source. */
    li  t6, 0x2
    sw  t6, 4(s1)
1:  lw  s2, 4(s1)
    li  s3, 0x8
    and s2, s2, s3
    beqz s2, 1b

    /* This core's own vector table, into shared SRAM. It has to land before
       mtvec is programmed, which happens right after this routine returns. */
    la a0, _v3f_vector_lma
    la a1, _v3f_vector_start
    la a2, _v3f_vector_end
    bgeu a1, a2, 3f
1:  lw t0, (a0)
    sw t0, (a1)
    addi a0, a0, 4
    addi a1, a1, 4
    bltu a1, a2, 1b
3:

    /* The V5F's vector table, into ITCM. It must be in RAM before that core
       takes its first interrupt -- see the comment in the linker script. */
    la a0, _vector_lma
    la a1, _vector_start
    la a2, _vector_end
    bgeu a1, a2, 3f
1:  lw t0, (a0)
    sw t0, (a1)
    addi a0, a0, 4
    addi a1, a1, 4
    bltu a1, a2, 1b
3:

    /* .itcm_text: the measured-hot code. Usually small, often empty. ITCM is
       tightly coupled to the V5F, but it is globally addressable and this core
       can write it, just with HCLK waits. */
    la a0, _itcm_lma
    la a1, _itcm_start
    la a2, _itcm_end
    bgeu a1, a2, 2f
1:  lw t0, (a0)
    sw t0, (a1)
    addi a0, a0, 4
    addi a1, a1, 4
    bltu a1, a2, 1b

    /* .data -- see the header comment. Without this the board is silent. */
2:  la a0, _data_lma
    la a1, _data_vma
    la a2, _edata
    bgeu a1, a2, 2f
1:  lw t0, (a0)
    sw t0, (a1)
    addi a0, a0, 4
    addi a1, a1, 4
    bltu a1, a2, 1b

2:  la a0, _sbss
    la a1, _ebss
    bgeu a0, a1, 2f
1:  sw zero, (a0)
    addi a0, a0, 4
    bltu a0, a1, 1b
2:
    ret
