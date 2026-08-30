"""Every sketch in tests/sketches must build and link.

This is the check that keeps working when the board does not -- a wedged debug
probe, an unplugged cable, someone else using the bench. It catches the whole
class of failures that are structural rather than behavioural: a missing
symbol, a section that no longer fits, a build option that stopped composing
with another one.

It does not replace tests/hw. It is what runs when tests/hw cannot.
"""
import subprocess
import pathlib
import shutil
import pytest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SKETCHES = ROOT / "tests" / "sketches"

pytestmark = pytest.mark.skipif(shutil.which("pio") is None,
                                reason="PlatformIO CLI not on PATH")


def _sketches():
    return sorted(p.name for p in SKETCHES.iterdir()
                  if (p / "platformio.ini").is_file())


@pytest.mark.parametrize("sketch", _sketches())
def test_sketch_links(sketch):
    r = subprocess.run(["pio", "run", "-d", str(SKETCHES / sketch)],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout[-5000:] + r.stderr[-5000:]


@pytest.mark.parametrize("sketch", _sketches())
def test_no_region_overflows(sketch):
    """--print-memory-usage is on for every build precisely so this is
    checkable. A region on this part will fill up."""
    r = subprocess.run(["pio", "run", "-d", str(SKETCHES / sketch)],
                       capture_output=True, text=True)
    out = r.stdout + r.stderr
    assert "region `" not in out or "overflowed" not in out, out[-3000:]


def test_usb_buffers_are_in_reachable_memory():
    """TinyUSB's .bss must be in USB_RAM, in the shared region.

    The USB controller's own bus master cannot see DTCM -- unlike DMA1/2, which
    reach everything -- so buffers left in .bss give a device that enumerates
    and then transfers nothing. The linker matches on object FILENAME because
    on Windows the paths it sees use backslashes and a `*/dir/*` wildcard
    matches nothing at all, silently.

    .usbram at zero is exactly that failure."""
    size = pathlib.Path.home() / ".platformio/packages/toolchain-riscv/bin/riscv-wch-elf-size.exe"
    elf = SKETCHES / "coretest/.pio/build/ch32h417/firmware.elf"
    if not size.is_file() or not elf.is_file():
        pytest.skip("toolchain or firmware.elf missing")
    out = subprocess.run([str(size), "-A", str(elf)],
                         capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        if line.startswith(".usbram"):
            used = int(line.split()[1])
            assert used > 1000, f".usbram is only {used} bytes"
            return
    raise AssertionError(".usbram section missing entirely")
