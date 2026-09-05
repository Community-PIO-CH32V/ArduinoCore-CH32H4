#!/usr/bin/env python3
"""Check that nothing in ITCM calls out of it.

    python tools/check_itcm.py <objdump> <firmware.elf>

WHY THIS EXISTS. ch32h4_ota_commit() erases the sketch region and then programs
it. From the first erase until the reset, every instruction it executes has to
come from somewhere other than that region -- so it and the flash primitives it
calls are __itcm_func, which puts them in ITCM.

The attribute is not enough on its own, because the compiler emits calls the
source does not contain:

  - -msave-restore moves function prologues into libgcc helpers
    (__riscv_save_N / __riscv_restore_N), which live in .text, in flash. The
    call is in the PROLOGUE, so it happens on entry to a function that looks
    perfectly self-contained.

  - loop-idiom recognition turns a hand-written word-at-a-time copy back into a
    call to memcpy. The code had a comment saying it was written that way
    *because* memcpy is in flash. The compiler undid it anyway.

Both of those shipped. The first page program after the erase jumped into
freshly erased flash, executed 0xE339E339, and left the flash controller
mid-operation -- a state in which the debug probe cannot attach either, so the
board needed a physical reset and a `wlink erase` to come back.

A comment cannot enforce this and a code review will not catch it, because the
offending instruction is not in the source. Checking the linked image is the
only place the truth is visible.
"""
import re
import subprocess
import sys


def sections(objdump, elf):
    """(name, vma, size) for every section, from objdump -h."""
    out = subprocess.check_output([objdump, "-h", elf],
                                  universal_newlines=True)
    found = []
    for line in out.splitlines():
        m = re.match(r"\s*\d+\s+(\S+)\s+([0-9a-f]+)\s+([0-9a-f]+)", line)
        if m:
            found.append((m.group(1), int(m.group(3), 16), int(m.group(2), 16)))
    return found


def check(objdump, elf):
    itcm = [s for s in sections(objdump, elf) if s[0] == ".itcm_text"]
    if not itcm:
        # No ITCM code in this image: nothing to check, and not an error --
        # a sketch that never touches flash never pulls the committer in.
        return []
    _, start, size = itcm[0]
    if size == 0:
        return []
    end = start + size

    out = subprocess.check_output(
        [objdump, "-d", "--start-address=0x%x" % start,
         "--stop-address=0x%x" % end, elf],
        universal_newlines=True)

    bad = []
    site = None
    for line in out.splitlines():
        m = re.match(r"\s*([0-9a-f]+):\s+\S+\s+(\S+)\s+(.*)", line)
        if not m:
            continue
        addr, mnemonic, rest = m.group(1), m.group(2), m.group(3)
        if mnemonic not in ("jal", "jalr", "j", "jr", "tail", "call"):
            continue

        # objdump resolves the destination either as a bare operand
        # ("jal 200a0254 <wait_bsy>") or, for an auipc/jalr pair, as a comment
        # ("jalr t0,-100(t1) # 272ee <__riscv_save_4>").
        t = re.search(r"#\s*([0-9a-f]+)\s+<([^>]+)>", rest)
        if not t:
            t = re.search(r"\b([0-9a-f]{2,})\s+<([^>]+)>", rest)
        if not t:
            # An indirect jump with no resolvable target. A register-indirect
            # call out of ITCM would be invisible here, but nothing in this
            # code makes one; the compiler-inserted calls this exists to catch
            # are all direct or auipc-relative.
            continue

        target, name = int(t.group(1), 16), t.group(2)
        if not (start <= target < end):
            bad.append((addr, mnemonic, name, target))
        site = site  # keep flake quiet about the unused var
    return bad


def main():
    if len(sys.argv) != 3:
        sys.stderr.write(__doc__)
        return 2
    objdump, elf = sys.argv[1], sys.argv[2]
    bad = check(objdump, elf)
    if not bad:
        return 0
    sys.stderr.write(
        "\nERROR: code in ITCM calls out of ITCM.\n\n"
        "These calls land in flash. Any of them reached while the flash is\n"
        "erased or busy executes garbage, and the board needs a probe and a\n"
        "`wlink erase` to recover:\n\n")
    for addr, mnemonic, name, target in bad:
        sys.stderr.write("  0x%s  %-5s -> %s (0x%x)\n"
                         % (addr, mnemonic, name, target))
    sys.stderr.write(
        "\nIf the name is __riscv_save_* or __riscv_restore_*, -msave-restore\n"
        "has come back into the build flags. If it is memcpy/memset/memmove,\n"
        "the compiler turned a hand-written loop into a libcall -- write the\n"
        "loop through a volatile pointer, or do without the copy.\n")
    return 1


if __name__ == "__main__":
    sys.exit(main())
