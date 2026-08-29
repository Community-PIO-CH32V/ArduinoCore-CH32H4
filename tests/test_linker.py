"""Checks on the linked image's memory map.

These run against the artifact of tests/sketches/minimal, so run test_build.py
first (or the whole suite, which is ordered).
"""
import subprocess
import pathlib
import shutil
import pytest

ROOT = pathlib.Path(__file__).resolve().parents[1]
ELF = ROOT / "tests/sketches/minimal/.pio/build/ch32h417/firmware.elf"

TOOLCHAIN = pathlib.Path.home() / ".platformio/packages/toolchain-riscv/bin"
NM = TOOLCHAIN / "riscv-wch-elf-nm.exe"
OBJCOPY = TOOLCHAIN / "riscv-wch-elf-objcopy.exe"

pytestmark = pytest.mark.skipif(
    not NM.is_file() or not ELF.is_file(),
    reason="toolchain or firmware.elf missing -- run test_build.py first")


def _syms():
    r = subprocess.run([str(NM), str(ELF)], capture_output=True, text=True, check=True)
    out = {}
    for line in r.stdout.splitlines():
        parts = line.split()
        if len(parts) == 3:
            out[parts[2]] = int(parts[0], 16)
    return out


def test_all_startup_symbols_exist():
    s = _syms()
    for name in ["_estack_v3f", "_estack_v5f", "_data_lma", "_data_vma", "_edata",
                 "_sbss", "_ebss", "_itcm_lma", "_itcm_start", "_itcm_end",
                 "_loadcode_lma", "_loadcode_vma_start", "_loadcode_vma_end",
                 "_heap_dtcm_start", "_heap_dtcm_end",
                 "_heap_shared_start", "_heap_shared_end",
                 "__eh_frame_start", "__eh_frame_end"]:
        assert name in s, f"{name} missing from the link"


def test_v5f_entry_is_1k_aligned():
    """NVIC_WakeUp_V5F masks the address with ~0x3FF and does not complain.
    An unaligned entry starts the core in the middle of whatever precedes it."""
    assert _syms()["_v5f_entry"] % 1024 == 0


def test_v5f_entry_matches_the_build_constant():
    assert _syms()["_v5f_entry"] == 0x00008000


def test_regions_are_where_the_spec_says():
    s = _syms()
    # DTCM: .data, .bss, the V5F stack and the fast half of the heap.
    assert 0x200C0000 <= s["_estack_v5f"] < 0x20100000
    assert 0x200C0000 <= s["_heap_dtcm_start"] < 0x20100000
    assert s["_heap_dtcm_end"] == 0x20100000
    # The shared region starts after the 52 KB of reserved DMA buffers.
    assert s["_heap_shared_start"] == 0x2010D000
    assert s["_heap_shared_end"] == 0x20180000


def test_heap_is_at_least_600k():
    s = _syms()
    total = ((s["_heap_dtcm_end"] - s["_heap_dtcm_start"])
             + (s["_heap_shared_end"] - s["_heap_shared_start"]))
    assert total > 600 * 1024, f"heap is only {total} bytes"


def test_binary_is_not_padded_by_a_progbits_stack(tmp_path):
    """A .stack section that only advances the location counter becomes
    PROGBITS at a RAM load address, and objcopy then pads the image out to it.
    That produced a 514 MB firmware.bin in the libhal port."""
    out = tmp_path / "firmware.bin"
    subprocess.run([str(OBJCOPY), "-O", "binary", str(ELF), str(out)], check=True)
    assert out.stat().st_size < 1024 * 1024, \
        f"image is {out.stat().st_size} bytes -- a NOLOAD section is missing"
