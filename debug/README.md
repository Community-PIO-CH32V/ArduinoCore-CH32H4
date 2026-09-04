# Debugging the CH32H41x

One OpenOCD configuration, `ch32h417.cfg`, which brings up both cores. Which
one the debugger attaches to is decided by the **client**, not by this
directory: cortex-debug's `targetProcessor`, set per programmer in
`programmers.txt`.

| `targetProcessor` | core | where it is |
|---|---|---|
| `1` | V5F, hart 1 | `setup()`, `loop()` — the sketch |
| empty (→ 0) | V3F, hart 0 | `setup1()`, `loop1()`, the boot stub |

## Ports are not 3333, and must not be pinned

The obvious design — give each target an explicit `-gdb-port`, one file per
core — is wrong here, and wrong in a way that looks like it works.

The Arduino IDE's debugger asks the OS for two consecutive free ports
somewhere in 50000–52000, hands the first to OpenOCD as `-c "gdb_port N"`, and
connects GDB to the port for the processor it wants. A target that carries its
own `-gdb-port` ignores that `-c` and keeps what the file said, so the IDE
connects to a port nothing is listening on.

Left alone, OpenOCD gives its first target `N` and its second `N+1`, which is
exactly what cortex-debug expects. A test asserts that no `-gdb-port` appears
in the config.

## What actually binds a core, which is not what it looks like

The hart bound to a target follows **the order the targets are created in**.
Measured, because two of the three obvious levers do nothing:

| | |
|---|---|
| `-coreid` | accepted and **ignored** by this OpenOCD's `wch_riscv` driver. Swapping the two values changes nothing; omitting them changes nothing. |
| target name | irrelevant. `cpu.0` is not hart 0 by virtue of being called that. |
| **creation order** | **this is the lever.** First created is hart 0, the V3F. |

So the order of the two `target create` lines is load-bearing, and a test
asserts it. Reorder them and the cores swap underneath the port numbers: the
debugger still attaches, halts, single-steps and shows source, and the source
is the other core's.

`$mhartid` is no help in telling them apart — read through this driver it
returns 0 for both. The V3F is identified by `PWR_EnterSTOPMode` under
`ch32h4_v3f_main`, the V5F by the sketch under `ch32h4_v5f_main`.

## Why processor 0 is written as an empty value

cortex-debug picks the port with

    createPortName = (e, t = "gdbPort") => t + (0 === e ? "" : e.toString())

and that comparison is strict. arduino-cli emits `debug.cortex-debug.custom.*`
values as JSON **strings**, so `"0"` is not `0`: it names `gdbPort0`, which is
not a key in the port map (`gdbPort` and `gdbPort1` are), and GDB is told to
connect to `localhost:undefined`.

An empty value is falsy, so cortex-debug's own `targetProcessor || 0` turns it
into the number `0`. `"1"` needs no such care, because it is never compared
against `0`. A test rejects a literal `"0"`.

## Why not the vendor's wch-dual-core.cfg

It is where the two-target structure here comes from. Two things about it do
not suit this core:

* Its work areas are at `0x20100000` and `0x20102800`. In this core's map
  those are `USB_RAM` and the middle of `ETH_RAM` — live descriptor memory
  that the USB and Ethernet bus masters write on their own. `-work-area-backup`
  saves and restores the region around an operation, so a restore would put
  stale bytes back over descriptors the hardware had updated meanwhile. This
  configuration declares no work area at all: nothing here flashes through
  OpenOCD, and reading memory and setting breakpoints does not need one.

* Its second flash bank is at `0x00005000`, which is where the vendor's own
  example puts a second core's image. This core's V5F image starts at
  `0x00008000`, and there is one flash, not two.

## Attaching, not launching

The launch configuration is `attach`. The V5F is started by the V3F, not by
the reset vector, so a debugger that resets the part and expects to find the
V5F running finds it halted at nothing. Upload first — with the normal Upload
button, which uses wlink — then attach.

Build with **Optimize for Debugging** (`arduino-cli compile
--optimize-for-debug`). Without it the build is `-Os` with no `-g` at all: a
breakpoint on a function works and nothing else does.

## `arduino-cli debug` does not work on Windows

The IDE runs OpenOCD as its own process and connects GDB over TCP, which is
what everything above describes and what was verified on hardware.
`arduino-cli debug` instead starts the server *through* GDB as a pipe, with
`-c "gdb_port pipe"`, and that path is blocked twice over:

* **The client.** xPack `riscv-wch-elf-gdb` 12.1 — the GDB in this core's
  toolchain — cannot spawn a pipe child on Windows at all:

      (gdb) target extended-remote | C:/Windows/System32/cmd.exe /c echo hi
      error starting child process: CreateProcess: No such file or directory

  with the file present, a shell on `PATH`, and with `SHELL` unset.

* **The server.** Using a GDB that *can* — `riscv32-wch-elf-gdb` 17.1 from
  MounRiver Studio's GCC15 toolchain spawns it fine — only moves the failure
  one layer down. With two targets declared, WCH's OpenOCD exits without a
  message immediately after examining the first. With the vendor's
  single-target `wch-riscv.cfg` it survives that and then fails the handshake:

      Error: GDB missing ack(2) - assumed good
      Remote replied unexpectedly to 'vMustReplyEmpty': vCont;c;C;s;S

  Both failures reproduce on MounRiver's own OpenOCD build (2026-07-23) as
  well as PlatformIO's (2026-02-25), so a newer server is not the answer
  either. And that same GDB 17.1 drives this core's config perfectly over TCP:
  it is the pipe path in OpenOCD, not the debugger and not this
  configuration.

  Note also which way the version skew cuts. GDB 12.1 tolerates the
  `vMustReplyEmpty` answer that GDB 13 and later reject, so the older GDB in
  this core's toolchain is the *better* client for this OpenOCD, not the
  worse one. There is no combination of the parts on this machine that makes
  `arduino-cli debug` work.

Start the server yourself instead. This is the whole procedure:

    openocd -f debug/ch32h417.cfg

    riscv-wch-elf-gdb build/Sketch.ino.elf
    (gdb) target extended-remote localhost:3333   # V3F, the first target
    (gdb) monitor halt
    (gdb) bt

With no `-c gdb_port`, OpenOCD falls back to 3333 for its first target and
3334 for its second, so **3334 is the V5F** — the opposite of the IDE's
numbering, where the V5F is the *second* port of a pair starting wherever the
OS had two free.
