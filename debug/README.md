# Debugging the CH32H41x

Two OpenOCD configurations, one per core, and the only difference between them
is which core answers on GDB port **3333**.

|                        | port 3333 | port 3334 |
|------------------------|-----------|-----------|
| `ch32h417-v5f.cfg`     | V5F (`mhartid` 1) | V3F (`mhartid` 0) |
| `ch32h417-v3f.cfg`     | V3F (`mhartid` 0) | V5F (`mhartid` 1) |

Port 3333 is the one that matters: every debugger front end connects there,
and neither the Arduino IDE nor `arduino-cli debug` offers a way to ask for
another. Selecting a core therefore means selecting a script, which is what
the two entries in `programmers.txt` do.

The other core stays on 3334 rather than being switched off. A second GDB can
attach to it at the same time, which is the only way to look at both halves of
a `setup1()`/`loop1()` sketch at once.

## Why not the vendor's wch-dual-core.cfg

It works, and it is where the port numbers here come from. Two things about it
do not suit this core:

* Its work areas are at `0x20100000` and `0x20102800`. In this core's map
  those are `USB_RAM` and the middle of `ETH_RAM` -- live descriptor memory
  that the USB and Ethernet bus masters write on their own. `-work-area-backup
  1` saves and restores the region around an operation, so a restore would put
  stale bytes back over descriptors the hardware had updated meanwhile. These
  configurations declare no work area at all: nothing here flashes through
  OpenOCD, and reading memory and setting breakpoints does not need one.

* Its second flash bank is at `0x00005000`, which is where the vendor's own
  example puts a second core's image. This core's V5F image starts at
  `0x00008000`, and there is one flash, not two.

## Which core is which

Read `mhartid`. The V3F is hart 0 and the V5F is hart 1 -- the same numbering
the console prints as `core_id=` at boot. Both cores symbolicate against the
same ELF, because the whole point of the single-image design is that there is
only one.

    (gdb) p/x $mhartid
    $1 = 0x1

## Attaching, not launching

The launch configuration is `attach`. The V5F is started by the V3F, not by
the reset vector, so a debugger that resets the part and expects to find the
V5F running finds it halted instead. Upload first -- with the normal Upload
button, which uses wlink -- and then attach to the running image.

## `arduino-cli debug` does not work on Windows, and it is not the config

The Arduino IDE drives OpenOCD as a separate process and connects GDB to it
over TCP, which is what everything above describes and what was verified on
hardware. `arduino-cli debug` does something different: it starts the server
through GDB itself, as a pipe, with `-c "gdb_port pipe"`.

The GDB in this toolchain -- xPack `riscv-wch-elf-gdb` 12.1 -- cannot spawn a
pipe child on Windows at all:

    (gdb) target extended-remote | C:/Windows/System32/cmd.exe /c echo hi
    error starting child process '| C:/Windows/System32/cmd.exe /c echo hi':
    CreateProcess: No such file or directory

That is with the file plainly present and a shell on `PATH`; it fails the same
way with `SHELL` unset. `riscv-none-embed-gdb` 8.3 and `riscv32-esp-elf-gdb`
17.1 on the same machine both spawn the child successfully, so it is this
build, not Windows and not the command line. Swapping in the 8.3 one is not a
fix either: it hangs on an image GCC 12 compiled, which is what a GDB five
major versions behind the DWARF it is being handed does.

Until the toolchain ships a GDB that can, start the server yourself. This is
the whole procedure, and it is what the measurements in this file were taken
with:

    openocd -f debug/ch32h417-v5f.cfg

    riscv-wch-elf-gdb build/Sketch.ino.elf
    (gdb) target extended-remote localhost:3333
    (gdb) monitor halt
    (gdb) bt

Port 3334 in the same session is the other core.
