/* V5F startup.
 *
 * Entered by NVIC_WakeUp_V5F() with no stack, no vector table and no CSRs
 * configured. The clock tree is already up: the V3F programmed every PLL
 * before waking this core, and reconfiguring PLLs underneath a running core is
 * exactly as bad as it sounds, so nothing here calls SystemInit().
 *
 * THIS CORE COPIES NOTHING. In one ELF the two cores share one .data and one
 * .bss, and the V3F -- which runs first -- has already initialised them. A
 * second .bss zero here would wipe the clock state the V3F just established.
 * See the header comment in startup_v3f.S.
 *
 * Symbols are prefixed so this file and startup_v3f.S can be linked together.
 * WCH's two stock startup files cannot: they define the same ~150 names, which
 * is the whole reason both prior ports to this silicon ship two ELFs.
 */

    .section .init_v5f, "ax", @progbits
    .global _start_v5f
    .align  2
_start_v5f:
    j handle_reset_v5f

    .section .vector_v5f, "ax", @progbits
    .align  6
_vector_base_v5f:
    .option norvc
    .word   _start_v5f
    .word   0
    .word   NMI_Handler
    .word   HardFault_Handler
    .rept   60
    .word   0
    .endr

    .section .text, "ax", @progbits

    .weak   NMI_Handler
    .weak   HardFault_Handler
    .align  2
NMI_Handler:
HardFault_Handler:
1:  j 1b

    .global handle_reset_v5f
    .align  2
handle_reset_v5f:
.option push
.option norelax
    la gp, __global_pointer$
.option pop

    la sp, _estack_v5f

    /* Prefetch and interrupt nesting. These differ from the V3F's: this core
       uses 0x1237B3E0 with nesting level 7 and 0x0F, against the V3F's
       0x123703E1, level 1 and 0x07. */
    li t0, 0x1237B3E0
    csrw 0xBC0, t0
    li t0, 0x07
    csrw 0xBC1, t0
    li t0, 0x0F
    csrw 0x804, t0

    /* FPU on, interrupts on, and STAY IN MACHINE MODE.
     *
     * WCH ships 0x6088, which leaves MPP=00 and returns into User mode, where
     * every M-mode CSR is unreachable. On the V3F that produced a board that
     * emitted nothing at all -- see docs/hazards.md.
     *
     * 0x7888 = FS=11 (FPU on) | MPP=11 (Machine mode) | MPIE | MIE. */
    li t0, 0x7888
    csrw mstatus, t0

    /* Mode 3: vectored, absolute addresses in the table. */
    la t0, _vector_base_v5f
    ori t0, t0, 3
    csrw mtvec, t0

    la t0, ch32h4_v5f_main
    csrw mepc, t0
    mret
