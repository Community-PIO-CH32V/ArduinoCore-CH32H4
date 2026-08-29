/* Stand-in. Task 3 replaces this with the real dual-core map; it exists only
   so the link succeeds while the PlatformIO integration is being verified. */
ENTRY( _start_v3f )
MEMORY {
  FLASH (rx)  : ORIGIN = 0x00000000, LENGTH = 960K
  RAM   (xrw) : ORIGIN = 0x200C0000, LENGTH = 256K
}
SECTIONS {
  .text : { *(.init) *(.text) *(.text.*) *(.rodata) *(.rodata.*) } >FLASH
  .data : { *(.data) *(.data.*) } >RAM AT>FLASH
  .bss  : { *(.bss) *(.bss.*) *(COMMON) } >RAM
}
