/* CH32H417QEU6 -- one ELF, both cores.
 *
 * The V3F boots at flash origin and wakes the V5F at _v5f_entry, which lives
 * in this same image. Both prior ports to this silicon ship two ELFs merged by
 * a script; they had to, because WCH's stock startup files for the two cores
 * define the same ~150 symbols. This core writes its own startup with prefixed
 * symbols, so one link produces one .bin and there is no merge step.
 *
 * NVIC_WakeUp_V5F() masks the entry address with ~0x3FF and does not complain,
 * so an unaligned entry starts the core in the middle of whatever precedes it.
 * That is asserted at the bottom of this file.
 *
 * Code runs XIP from flash behind the V5F's 32 KB I-cache. Only functions
 * measured as hot are placed in ITCM, via __itcm_func. See
 * docs/superpowers/specs/2026-08-29-ch32h4-arduino-core-design.md section 4.
 */
ENTRY( _start_v3f )

/* 8 KB, not the 2 KB this was while the V3F only ever ran the boot stub.
 * setup1()/loop1() are ordinary user code and can call anything -- printf
 * alone can want a kilobyte. DTCM has room: the heap gives up 6 KB out of
 * roughly 240. */
__stack_size_v3f = 8192;
__stack_size_v5f = 16384;

MEMORY
{
    /* The V3F stub: vectors, clocks, VIO18, console, wake. It is small and does
       not grow, so 32 K is generous and keeps _v5f_entry on a round number. */
    FLASH_V3F (rx)  : ORIGIN = 0x00000000, LENGTH = 32K

    /* The Arduino image. This region deliberately STOPS at the EEPROM base
       rather than running to the end of the 960 K user area, so an oversized
       image is a link error instead of a board that works until someone writes
       a setting. MicroPython lost 26 K of image into its filesystem for want
       of exactly this, and boot-looped on the next reset. */
    FLASH_V5F (rx)  : ORIGIN = 0x00008000, LENGTH = 912K

    /* ITCM: zero-wait at the V5F core clock. NOT where code goes by default --
       only what has been measured as hot. */
    ITCM (xrw)      : ORIGIN = 0x200A0000, LENGTH = 128K

    /* DTCM: zero-wait at the V5F core clock. .data, .bss, the V5F stack and the
       fast half of the heap. The V3F reaches it with HCLK waits, which is fine
       for the stub. */
    RAM_LOAD (xrw)  : ORIGIN = 0x200C0000, LENGTH = 512
    DTCM (xrw)      : ORIGIN = 0x200C0200, LENGTH = 256K - 512

    /* Shared SRAM: the only memory both cores reach at speed, and the only
       memory the USB controller's own bus master can reach at all. (DMA1/2
       reach every region including DTCM -- MEMARY_CFGR, CSR 0xBC5, resets to
       read+write enabled -- so these reservations are about the USB and
       Ethernet masters, not about DMA in general.)

       These four are carved out now, in M1, so the map does not churn under
       later milestones. The sizes are the MicroPython port's measured ones. */
    USB_RAM (xrw)   : ORIGIN = 0x20100000, LENGTH = 8K     /* M2 */
    ETH_RAM (xrw)   : ORIGIN = 0x20102000, LENGTH = 28K    /* M6 */
    SD_RAM (rw)     : ORIGIN = 0x20109000, LENGTH = 8K     /* M5 */
    XCORE_RAM (xrw) : ORIGIN = 0x2010B000, LENGTH = 8K     /* M4 */
    SHARED (xrw)    : ORIGIN = 0x2010D000, LENGTH = 512K - 52K
}

SECTIONS
{
    /* ---- V3F image ------------------------------------------------------ */

    .v3f_init : {
        . = ALIGN(4);
        KEEP(*(SORT_NONE(.init_v3f)))
        . = ALIGN(4);
    } >FLASH_V3F

    .v3f_vector : {
        . = ALIGN(64);
        KEEP(*(.vector_v3f))
        . = ALIGN(4);
    } >FLASH_V3F

    .v3f_text : {
        . = ALIGN(4);
        *(.text.v3f)
        *(.text.v3f.*)
        *(.rodata.v3f)
        *(.rodata.v3f.*)
        . = ALIGN(4);
    } >FLASH_V3F

    /* The V3F copies this to RAM and calls it there. It reprograms the PLL,
       and changing the PLL while instructions are being fetched from flash
       changes the flash access timing underneath the fetch. It also carries
       the .data / .bss / .itcm_text copies, which this core owns -- see the
       header comment in startup_v3f.S for why the second core must not repeat
       them. */
    .loadcodealign : {
        . = ALIGN(4);
        _loadcode_lma = .;
    } >FLASH_V3F

    .loadcode : {
        . = ALIGN(4);
        _loadcode_vma_start = .;
        *(.load)
        . = ALIGN(4);
        _loadcode_vma_end = .;
    } >RAM_LOAD AT>FLASH_V3F

    /* ---- V5F image ------------------------------------------------------ */

    .v5f_init : {
        . = ALIGN(1024);
        _v5f_entry = .;
        KEEP(*(SORT_NONE(.init_v5f)))
        . = ALIGN(4);
    } >FLASH_V5F

    /* The V5F's vector table lives in RAM, not flash.
     *
     * In mtvec mode 3 the trap hardware reads an absolute address out of this
     * table at the moment an interrupt fires. That read does not go through
     * the path ordinary instruction fetch uses, and from flash it returns
     * garbage: the core jumps to a nonsense address and takes an illegal
     * instruction (mcause=2, mepc=0x800800 was the observed pair), which then
     * traps again. With mtvec still reading 0 at that point the second trap
     * lands at address 0 -- which is _start_v3f -- so the V5F re-runs the V3F's
     * startup and the board appears to boot-loop.
     *
     * Both prior ports to this silicon put .vector in RAM_CODE for the same
     * reason; neither says why, and this is why.
     *
     * It goes in ITCM rather than the shared region because it is read on
     * every interrupt and ITCM is zero-wait at the V5F's core clock. The V3F
     * copies it in _load_base_v3f, alongside .data and .itcm_text. */
    .vectoralign : {
        . = ALIGN(4);
        _vector_lma = .;
    } >FLASH_V5F

    .v5f_vector : {
        . = ALIGN(64);
        _vector_start = .;
        KEEP(*(.vector_v5f))
        . = ALIGN(4);
        _vector_end = .;
    } >ITCM AT>FLASH_V5F


    /* The instruction cache covers exactly this span, bounded by a PMP entry
       -- see the enable sequence in startup_v5f.S. It brackets the flash-
       resident code and rodata, which is what actually benefits: ITCM is
       already zero-wait and is deliberately left outside the window. */
    .text : {
        . = ALIGN(4);
        _cache_beg = .;
        *(.text.unlikely .text.unlikely.*)
        *(.text.startup .text.startup.*)
        *(.text .text.*)
        *(.gnu.linkonce.t.*)
        . = ALIGN(4);
    } >FLASH_V5F

    .rodata : {
        . = ALIGN(4);
        *(.rdata)
        *(.rodata .rodata.*)
        *(.gnu.linkonce.r.*)
        . = ALIGN(4);
        _cache_end = .;
    } >FLASH_V5F

    .init_array : {
        . = ALIGN(4);
        __init_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.init_array.*)))
        KEEP(*(.init_array))
        __init_array_end = .;
        . = ALIGN(4);
    } >FLASH_V5F

    .fini_array : {
        . = ALIGN(4);
        __fini_array_start = .;
        KEEP(*(SORT_BY_INIT_PRIORITY(.fini_array.*)))
        KEEP(*(.fini_array))
        __fini_array_end = .;
        . = ALIGN(4);
    } >FLASH_V5F

    /* The unwind tables stay in flash: nothing reads them until something
       throws, a throw already costs microseconds of table walking, and this
       keeps roughly 30 K out of RAM. __eh_frame_start is handed to
       __register_frame_info by a priority-101 constructor, because under
       -nostartfiles nothing else registers it and every throw would otherwise
       reach std::terminate after a perfectly clean link. */
    .eh_frame : {
        . = ALIGN(4);
        __eh_frame_start = .;
        KEEP(*(.eh_frame))
        /* The terminating zero word.
         *
         * The registry-based unwinder walks CIE/FDE records until it reads a
         * zero length, and normally crtend.o supplies that word as
         * __FRAME_END__. Under -nostartfiles crtend is not linked, so without
         * this the unwinder runs off the end of the table and dereferences
         * whatever follows: the first throw takes a load access fault
         * (mcause=5) with mtval holding the garbage pointer, instead of being
         * caught. It links cleanly either way. */
        LONG(0)
        __eh_frame_end = .;
        . = ALIGN(4);
    } >FLASH_V5F

    .gcc_except_table : {
        . = ALIGN(4);
        *(.gcc_except_table .gcc_except_table.*)
        . = ALIGN(4);
    } >FLASH_V5F

    /* ---- ITCM: measured-hot code only ----------------------------------- */

    .itcmalign : {
        . = ALIGN(4);
        _itcm_lma = .;
    } >FLASH_V5F

    .itcm_text : {
        . = ALIGN(4);
        _itcm_start = .;
        *(.itcm_text)
        *(.itcm_text.*)
        . = ALIGN(4);
        _itcm_end = .;
    } >ITCM AT>FLASH_V5F

    /* ---- USB ------------------------------------------------------------ */

    /* TinyUSB's .bss, relocated into the shared region.
     *
     * The USB controller's own bus master cannot reach DTCM -- unlike DMA1/2,
     * which reach everything -- so every endpoint buffer, transfer descriptor
     * and class-driver state variable has to live here.
     *
     * That puts it OUTSIDE the _sbss.._ebss range the startup code clears, and
     * nothing else zeroes it. ch32h4_usb_init() does so explicitly using these
     * symbols. Skipping that leaves TinyUSB starting from whatever survived in
     * RAM, which presents as USB working or not depending on the build. */
    .usbram (NOLOAD) : {
        . = ALIGN(8);
        _susbram = .;
        /* Matched on FILENAME, not path.
           
           On Windows the linker sees object paths with backslashes --
           `.piouild\ch32h417\FrameworkTinyUSB	usb.o` -- so any wildcard
           written with forward slashes matches nothing at all, silently. The
           build script names every TinyUSB object tusb*.o precisely so this
           pattern can be path-independent.
           
           Getting this wrong is not a link error: TinyUSB's buffers simply
           stay in DTCM, which the USB controller's bus master cannot reach,
           and the device enumerates but transfers nothing. `size -A` showing
           .usbram at 0 is the tell. */
        *tusb*.o(.bss .bss.* COMMON)
        . = ALIGN(8);
        _eusbram = .;
    } >USB_RAM

    /* ---- Ethernet -------------------------------------------------------- */

    /* Descriptors and MAC frame buffers, in the shared region.
     *
     * The Ethernet DMA is on the system bus and cannot reach DTCM, so this
     * cannot live in .bss -- which also means the startup code's .bss clear
     * does not reach it, and eth_init() zeroes it explicitly using these
     * symbols. Descriptors want 32-byte alignment because the DMA reads them
     * as bursts.
     *
     * 28 KB holds 16 descriptors plus 12 RX and 4 TX frame buffers. The RX
     * ring is what absorbs a burst while the sketch is busy elsewhere, so
     * shrinking it trades heap for dropped frames. */
    .ethram (NOLOAD) : {
        . = ALIGN(32);
        _sethram = .;
        *(.ethram)
        *(.ethram.*)
        . = ALIGN(32);
        _eethram = .;
    } >ETH_RAM

    /* ---- Cross-core state ----------------------------------------------- */

    /* The shared region is the only memory both cores reach at speed, and the
       V5F's I-cache is not coherent with anything, so cross-core state cannot
       live in DTCM or be inlined into cached code. */
    .xcore (NOLOAD) : {
        . = ALIGN(8);
        *(.xcore)
        *(.xcore.*)
        . = ALIGN(8);
    } >XCORE_RAM

    /* ---- Data ----------------------------------------------------------- */

    .dataalign : {
        . = ALIGN(8);
        _data_lma = .;
    } >FLASH_V5F

    .data : {
        . = ALIGN(8);
        _data_vma = .;
        *(.data .data.*)
        *(.gnu.linkonce.d.*)
        . = ALIGN(8);
        __global_pointer$ = . + 0x800;
        *(.sdata .sdata.*)
        *(.sdata2 .sdata2.*)
        *(.gnu.linkonce.s.*)
        . = ALIGN(8);
        _edata = .;
    } >DTCM AT>FLASH_V5F

    .bss (NOLOAD) : {
        . = ALIGN(8);
        _sbss = .;
        *(.sbss .sbss.*)
        *(.gnu.linkonce.sb.*)
        *(.bss .bss.*)
        *(.gnu.linkonce.b.*)
        *(COMMON)
        . = ALIGN(8);
        _ebss = .;
    } >DTCM

    /* NOLOAD on both stacks, or a section that merely advances the location
       counter becomes PROGBITS at a RAM load address and objcopy pads the .bin
       out to it. That produced a 514 MB firmware.bin in the libhal port. */
    .v3f_stack (NOLOAD) : {
        . = ALIGN(16);
        . = . + __stack_size_v3f;
        . = ALIGN(16);
        _estack_v3f = .;
    } >DTCM

    .v5f_stack (NOLOAD) : {
        . = ALIGN(16);
        . = . + __stack_size_v5f;
        . = ALIGN(16);
        _estack_v5f = .;
    } >DTCM

    /* ---- Heap ----------------------------------------------------------- */

    /* Two disjoint regions. _sbrk hands out DTCM first -- zero-wait at the
       V5F's 400 MHz -- then continues into the shared region, which runs at
       HCLK and is larger. newlib's dlmalloc starts a NEW SEGMENT when sbrk
       returns memory that is not contiguous with the previous top, rather than
       assuming contiguity, so no allocation ever spans the gap. */
    _heap_dtcm_start = ALIGN(_estack_v5f, 8);
    _heap_dtcm_end = ORIGIN(DTCM) + LENGTH(DTCM);
    _heap_shared_start = ORIGIN(SHARED);
    _heap_shared_end = ORIGIN(SHARED) + LENGTH(SHARED);

    /DISCARD/ : {
        *(.note.*)
        *(.comment)
        *(.eh_frame_hdr)
    }
}

ASSERT( _v5f_entry % 1024 == 0,
    "The V5F entry must be 1 KB aligned: NVIC_WakeUp_V5F masks it with ~0x3FF and starts the core mid-instruction without complaining." )

ASSERT( _heap_dtcm_end > _heap_dtcm_start,
    "DTCM is full: .data, .bss and the two stacks leave no room for a heap." )
