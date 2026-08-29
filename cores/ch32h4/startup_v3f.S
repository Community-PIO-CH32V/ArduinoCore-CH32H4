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

/* The V3F's own vector table. Interrupt handling on this core arrives with
 * setup1()/loop1() in M4; until then the table only has to be valid, so the
 * fault entries trap visibly rather than running into whatever follows. */
    .section .vector_v3f, "ax", @progbits
    .align  6
_vector_base_v3f:
    .option norvc
    .word   _start_v3f
    .word   0
    .word   NMI_Handler_v3f
    .word   HardFault_Handler_v3f
    .rept   60
    .word   0
    .endr

    .section .text.v3f, "ax", @progbits

    .weak   NMI_Handler_v3f
    .weak   HardFault_Handler_v3f
    .align  2
NMI_Handler_v3f:
HardFault_Handler_v3f:
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

    /* Prefetch and interrupt-nesting configuration. These values are
       V3F-specific: the V5F uses 0x1237B3E0 with nesting level 7 and 0x0F
       here, this core 0x123703E1, level 1 and 0x07. WCH's two startup files
       differ from each other in exactly these registers plus the PLL. */
    li t0, 0x123703E1
    csrw 0xBC0, t0
    li t0, 0x01
    csrw 0xBC1, t0

    /* Interrupt nesting and the hardware stack. */
    li t0, 0x07
    csrw 0x804, t0

    /* FPU on, interrupts on, and STAY IN MACHINE MODE.
     *
     * WCH's own startup writes 0x6088 here, which leaves MPP=00 -- so the mret
     * below drops into User mode, where every M-mode CSR is unreachable.
     * __disable_irq() and friends then link cleanly and take an
     * illegal-instruction trap (mcause=2) the first time they run, which lands
     * in a fault handler that cannot print yet. The board says nothing at all.
     *
     * 0x7888 = FS=11 (FPU on) | MPP=11 (stay in Machine mode) | MPIE | MIE.
     * Both cores have a hardware single-precision FPU (misa = 0x40901127) and
     * the ABI is ilp32f, so FS is not optional either. */
    li t0, 0x7888
    csrw mstatus, t0

    /* Mode 3: vectored, absolute addresses in the table. */
    la t0, _vector_base_v3f
    ori t0, t0, 3
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
