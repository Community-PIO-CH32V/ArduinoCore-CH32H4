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

/* Force the V5F's startup into the link.
 *
 * ENTRY() above references _start_v3f, which is what drags startup_v3f.o out
 * of the core archive. NOTHING references the V5F's equivalent: the V3F wakes
 * that core by writing an ADDRESS -- CH32_V5F_START_ADDR -- not by calling a
 * symbol. So without this the archive member is never extracted, and the image
 * links cleanly with no V5F reset handler and no V5F vector table. The board
 * flashes, the V3F boots, prints its banner, wakes a core that has no entry
 * point, and stops. Nothing in the build says a word.
 *
 * This used to be hidden by -Wl,--whole-archive around the core, which pulled
 * in every member whether referenced or not. That had to go so that unused
 * HardwareSerial ports are not linked, and EXTERN is the narrow version of it:
 * name what genuinely has no caller, rather than keeping everything because
 * two files do.
 *
 * KEEP() in the section list below is NOT a substitute. It stops --gc-sections
 * discarding a section that was linked; it cannot pull in an object the linker
 * never had a reason to read. */
EXTERN( _start_v5f )
EXTERN( _vector_base_v5f )

/* And the heap.
 *
 * cores/ch32h4/ch32h4_sbrk.c replaces newlib's _sbrk with one that hands out
 * DTCM first and then the shared SRAM -- two disjoint regions, which is the
 * only way to use both on this part. Nothing in the core CALLS _sbrk though;
 * malloc does, and malloc arrives from libc after the core archive has already
 * been scanned. Without this the linker resolves _sbrk from --specs=nosys
 * instead, and that stub wants a symbol `end` this layout does not define:
 *
 *     (.text._sbrk+0x10): undefined reference to `end'
 *
 * Naming it here makes _sbrk undefined from the start, so the core's own
 * definition is the one that satisfies malloc. Note the failure was loud;
 * the version where nosys DOES link and malloc quietly gets a heap of
 * whatever follows the image would not have been. */
EXTERN( _sbrk )

/* And __gnu_cxx::__verbose_terminate_handler, for the same reason and with a
 * more expensive symptom.
 *
 * cores/ch32h4/ch32h4_eh.cpp replaces libstdc++'s default handler, whose only
 * job is to print the type name of an escaping exception -- and which drags in
 * vterminate.o and the C++ name demangler, 43 KB of .text nothing else calls.
 * Nothing in the core references the replacement: libstdc++ finds it by name
 * or falls back to its own.
 *
 * With --whole-archive gone, "falls back to its own" is what happened, and the
 * image grew 36 KB with no error and no warning. Naming the mangled symbol
 * here is what keeps the override in the link. */
EXTERN( _ZN9__gnu_cxx27__verbose_terminate_handlerEv )

/* And the C++ runtime hooks, which fail in the worst way this part offers.
 *
 * cores/ch32h4/ch32h4_cxx.cpp supplies __cxa_atexit, atexit,
 * __cxa_pure_virtual, __cxa_deleted_virtual and __dso_handle. Under
 * -nostartfiles nothing else does, and nothing in the core CALLS them -- the
 * compiler emits the references, from any sketch with a function-local static
 * or a virtual destructor.
 *
 * Unresolved, those references become calls to ADDRESS 0. Address 0 on this
 * part is _start_v3f, so the call does not fault: the core quietly re-runs the
 * other core's startup, and the board lockup-resets several times a second
 * while printing a clean boot banner each time. `static String line;` in a
 * loop() is enough to trigger it.
 *
 * tests/test_link_matrix.py::test_nothing_calls_address_zero exists for this
 * and is what caught it when --whole-archive was removed. */
EXTERN( __cxa_atexit )
EXTERN( __cxa_pure_virtual )
EXTERN( __cxa_deleted_virtual )
EXTERN( __dso_handle )

/* 8 KB, not the 2 KB this was while the V3F only ever ran the boot stub.
 * setup1()/loop1() are ordinary user code and can call anything -- printf
 * alone can want a kilobyte. DTCM has room: the heap gives up 6 KB out of
 * roughly 240. */
__stack_size_v3f = 8192;
/* 32 KB, not the 16 KB this started as. lwIP's receive path is a deep C call
 * chain, and with the USB device task running from yield() on top of it 16 KB
 * overflowed: libhal measured 16656 bytes and went to 28 KB, and MicroPython
 * gives the V5F 32 KB with an 8 KB margin for exactly this reason. */
__stack_size_v5f = 32768;

/* Filesystem size, in bytes.
 *
 * THIS FILE IS A TEMPLATE. The filesystem-size token in the assignment below
 * is substituted with a byte count before the link, and the script does not
 * assemble until it has been:
 *
 *   PlatformIO      tools/platformio-build.py, from board_build.filesystem_size
 *   Arduino IDE     tools/simplesub.py, from a prelink hook in platform.txt
 *
 * One token, one meaning, both builds -- which is the arrangement arduino-pico
 * uses and the reason its memmap templates carry __FS_START__ and friends.
 * Substituting nothing would leave a linker script that fails on an undefined
 * symbol, which is a much better failure than one that silently links a
 * filesystem of length zero over the top of the sketch.
 *
 * Must be a multiple of the 8 KB flash erase page. The build checks that; this
 * file cannot, because ld has no way to report a good message from a MEMORY
 * expression. */
__fs_size = __CH32H4_FS_SIZE__;

MEMORY
{
    /* The V3F stub: vectors, clocks, VIO18, console, wake. It is small and does
       not grow, so 32 K is generous and keeps _v5f_entry on a round number. */
    FLASH_V3F (rx)  : ORIGIN = 0x00000000, LENGTH = 32K

    /* The Arduino image. This region deliberately STOPS at the filesystem
       rather than running to the end of the 960 K user area, so an oversized
       image is a link error instead of a board that works until someone writes
       a setting. MicroPython lost 26 K of image into its filesystem for want
       of exactly this, and boot-looped on the next reset.

       __fs_size is substituted by the build for board_build.filesystem_size
       and defaults to zero. The flash tail therefore looks like this, which is
       the layout arduino-pico uses:

           0x08008000  the sketch          912K - __fs_size
                       LittleFS            __fs_size   (selectable, may be 0)
           0x080EC000  EEPROM              16K         (always last)
           0x080F0000  end of user flash

       The EEPROM sits at the very end so that changing the filesystem size
       never moves it -- a stored setting survives a rebuild that resizes the
       filesystem, which is the whole reason arduino-pico pins its EEPROM to
       the top too. */
    FLASH_V5F (rx)  : ORIGIN = 0x00008000, LENGTH = 912K - __fs_size

    /* ITCM: zero-wait at the V5F core clock. NOT where code goes by default --
       only what has been measured as hot. */
    ITCM (xrw)      : ORIGIN = 0x200A0000, LENGTH = 128K

    /* The hardware interrupt stack. Reserved, never allocated.
     *
     * With HWSTKEN set in intsyscr the V5F saves the caller-saved registers to
     * memory on every trap entry, at the address in hw_popdm_addr (CSR 0xBC4).
     * That register's [31:20] field is read-only, so the region is always in
     * the TCM window, and on this part it reads 0x200C0000 out of reset -- the
     * DTCM base. (The QingKeV5 manual documents the reset value as 0x200A0000;
     * that is not what this silicon reports. Measured, not assumed.)
     *
     * Nothing announces this. The first version of this file put .loadcode
     * here, and a dump of 0x200C0000 taken before and after the first
     * interrupts showed saved registers landing on top of it -- a return
     * address and a live USART1 base among them. .load only runs once, during
     * the V3F's startup, so it survived; anything else here would not have.
     *
     * 512 bytes is what WCH's own V5F linker script leaves free at this
     * address, and it matches eight nesting levels of a 64-byte frame. */
    HW_STACK (rw)   : ORIGIN = 0x200C0000, LENGTH = 512

    RAM_LOAD (xrw)  : ORIGIN = 0x200C0200, LENGTH = 512

    /* DTCM: zero-wait at the V5F core clock. .data, .bss, both stacks and the
       fast half of the heap. The V3F reaches it with HCLK waits, which is fine
       for the stub. */
    DTCM (xrw)      : ORIGIN = 0x200C0400, LENGTH = 256K - 1K

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

    /* The V3F's vector table, in RAM for the same reason the V5F's is: in
     * mtvec mode 3 the trap hardware reads an absolute address out of the
     * table, and that read from flash returns garbage. The V3F got away with a
     * table in flash for as long as it never took a trap -- it wakes the V5F
     * and sleeps -- but the moment setup1()/loop1() run it does, and the board
     * lockup-resets with no fault record at all. WCH's own V3F linker script
     * puts .vector in shared SRAM; so does this. */
    V3F_VECTOR (xrw): ORIGIN = 0x2010D000, LENGTH = 1K
    SHARED (xrw)    : ORIGIN = 0x2010D400, LENGTH = 512K - 52K - 1K
}

SECTIONS
{
    /* ---- V3F image ------------------------------------------------------ */

    .v3f_init : {
        . = ALIGN(4);
        KEEP(*(SORT_NONE(.init_v3f)))
        . = ALIGN(4);
    } >FLASH_V3F

    .v3f_vectoralign : {
        . = ALIGN(4);
        _v3f_vector_lma = .;
    } >FLASH_V3F

    .v3f_vector : {
        . = ALIGN(64);
        _v3f_vector_start = .;
        KEEP(*(.vector_v3f))
        . = ALIGN(4);
        _v3f_vector_end = .;
    } >V3F_VECTOR AT>FLASH_V3F

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

        /* THIS IS A MEMORY BUDGET, NOT A CORRECTNESS REQUIREMENT, and it was
           written here for years as though it were the opposite.

           The comment this replaces said the USB controller's bus master
           cannot see DTCM, so buffers left there would give a device that
           enumerates and transfers nothing. That is true of the Ethernet DMA,
           which is why .ethram below exists, and it was assumed to be true of
           USB by analogy. It is not: building with every TinyUSB buffer in
           DTCM -- .usbram empty -- gives a device that enumerates, transfers,
           and passes all seven of tests/hw/test_usb.py. Measured, on hardware,
           rather than reasoned about.

           What the placement is actually worth: USB_RAM is 8 KB of the shared
           region that nothing else claims, and putting a few kilobytes of
           TinyUSB there is a few kilobytes of DTCM back. Small, free, and kept
           for that reason -- but if it ever gets in the way, it can go, and
           nothing will break.

           .usbram is NOLOAD and outside the _sbss.._ebss range the startup
           code clears, so ch32h4_usb_init() zeroes it explicitly. That part
           IS load-bearing: skipping it leaves TinyUSB starting from whatever
           survived in RAM. */

        /* By SECTION. cores/ch32h4/tusb_config.h defines CFG_TUSB_MEM_SECTION
           as .usbram and the CH32 device driver marks its endpoint buffers
           with it, so this rule means the same thing under every build system
           and under LTO. */
        *(.usbram)
        *(.usbram.*)

        /* And by FILENAME, which sweeps up the rest of TinyUSB's .bss.

           This one only matches under PlatformIO, whose build script renames
           every TinyUSB object to tusb*.o for the purpose; arduino-cli names
           them after their sources -- usbd.c.o, dcd_ch32_usbfs.c.o -- and an
           LTO link replaces both with ccXXXXXX.ltrans0.ltrans.o. So it is a
           bonus where it applies rather than something to rely on, which is
           exactly why the section rule above was added. Matched on the
           filename and not the path because on Windows the linker sees object
           paths with backslashes, and a wildcard written with forward slashes
           matches nothing at all, silently. */
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

    /* The SDMMC controller's own DMA reaches the shared region and not DTCM,
       the same limit the USB and Ethernet masters have. Its bounce buffers
       have to be 16-byte aligned, so the section is too. */
    .sdram (NOLOAD) : {
        . = ALIGN(16);
        _ssdram = .;
        *(.sdram)
        *(.sdram.*)
        . = ALIGN(16);
        _esdram = .;
    } >SD_RAM

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
    /* Directly below the V5F's stack, with no guard between them: an
       overflow on either lands in the other. Sized generously for that reason
       -- this core runs a stub and setup1()/loop1(), and 8 KB is far more than
       either needs. */
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

    /* ---- The flash tail: filesystem, then EEPROM ------------------------ */

    /* These are PHYSICAL addresses, 0x08000000-based. The FLASH_V3F/FLASH_V5F
       regions above are the same memory seen through the alias at zero that
       the part boots from; the flash controller's erase and program registers
       take the 0x08... form, so anything self-programming wants these.
       Confusing the two is a write to an address that is a valid mapping of
       something else entirely.

       Names follow arduino-pico, so its LittleFS and EEPROM code reads the
       same here: _FS_start, _FS_end, _EEPROM_start. */
    _flash_physical_base = 0x08000000;

    /* Always the last 16 KB, whatever the filesystem size. Two 8 KB erase
       pages, used alternately, because a 4 KB Arduino-style EEPROM cannot be
       one page on this part and a single-page read-modify-write has a window
       in which a power cut loses everything. */
    _EEPROM_size  = 16K;
    _EEPROM_start = _flash_physical_base + 960K - _EEPROM_size;
    _EEPROM_end   = _EEPROM_start + _EEPROM_size;

    /* Immediately below it, growing downward as it is made larger, so the
       EEPROM never moves and a stored setting survives a rebuild that resizes
       the filesystem. Empty by default: _FS_start == _FS_end. */
    _FS_end   = _EEPROM_start;
    _FS_start = _FS_end - __fs_size;

    /* The sketch region, named so that nothing has to hardcode 0x08008000.
       An over-the-air update erases exactly this and no more: below it is the
       V3F stub that brings the part up, above it the filesystem and the
       EEPROM, and an updater that ran past either end would take out the boot
       code or the user's stored settings. */
    _sketch_start = _flash_physical_base + ORIGIN(FLASH_V5F);
    _sketch_end   = _FS_start;

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

/* The image, the filesystem and the EEPROM must exactly tile the flash tail.
 *
 * Until now the EEPROM's base was a constant in EEPROM.h and the image's end
 * was a length here, and NOTHING checked the two against each other -- the
 * header said so in as many words. Get them out of step and the filesystem
 * either overlaps the sketch, which corrupts on the first write, or leaves a
 * silent hole. Both are far easier to cause than to diagnose.
 *
 * This is the check. It costs nothing and it fails at link time. */
ASSERT( _FS_start == _flash_physical_base + ORIGIN(FLASH_V5F) + LENGTH(FLASH_V5F),
    "The filesystem does not start where the sketch region ends. __fs_size and the FLASH_V5F length have gone out of step." )

ASSERT( _FS_end == _EEPROM_start,
    "The filesystem must end exactly where the EEPROM begins." )

ASSERT( _EEPROM_end == _flash_physical_base + 960K,
    "The EEPROM must be the last thing in flash." )

/* 8 KB is the erase page on the 960 KB part. A filesystem that is not a whole
 * number of pages has a block LittleFS can write but cannot erase. */
ASSERT( __fs_size % 8K == 0,
    "board_build.filesystem_size must be a multiple of the 8 KB flash erase page." )
