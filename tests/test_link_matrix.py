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


OBJDUMP = (pathlib.Path.home()
           / ".platformio/packages/toolchain-riscv/bin/riscv-wch-elf-objdump.exe")

_CALL = ("jal", "jalr", "call", "tail", "j")

# Functions allowed to contain a call to address zero, because they use the
# optional-hook idiom: a weak declaration, a null check, and a call the linker
# resolves to zero and the null check makes unreachable.
OPTIONAL_HOOK_CALLERS = {
    "yield",              # ch32h4_ticker_update, ch32h4_net_update
    "ch32h4_v3f_main",    # setup1, loop1
}


def _calls_to_zero(elf: pathlib.Path) -> list[str]:
    """Every call/jump in the image whose statically resolved target is 0.

    objdump renders a resolved target as a hex address immediately followed by
    the symbol it lands in -- `jal ra,d1b8 <atexit>`, or for an auipc/jalr pair
    `jalr zero # 0 <_start_v3f>`. Anchoring on that `<symbol>` is what keeps
    `jalr ra,0(a5)` -- an ordinary indirect call through a register -- from
    reading as a call to zero.

    Calls inside OPTIONAL_HOOK_CALLERS are skipped. Those functions use the
    weak-symbol idiom deliberately:

        extern void setup1(void) __attribute__((weak));
        if (setup1) { setup1(); }

    An undefined weak symbol IS address zero, so the call instruction is
    emitted with a zero target -- and the `if` in front of it means the
    instruction is unreachable. That is the intended shape, not a bug. Keeping
    the list short and explicit is the point: a call to zero anywhere else is
    the compiler emitting one nothing null-checks, which is the failure this
    test exists for.
    """
    out = subprocess.run([str(OBJDUMP), "-d", str(elf)],
                         capture_output=True, text=True, check=True).stdout
    bad = []
    function = "?"
    for line in out.splitlines():
        stripped = line.strip()
        if stripped.endswith(">:") and "<" in stripped:
            function = stripped[stripped.index("<") + 1:-2]
            continue
        fields = line.split("	")
        if len(fields) < 3 or fields[2].strip() not in _CALL:
            continue
        operands = "	".join(fields[3:])
        if "<" not in operands:
            continue                      # indirect: nothing to resolve
        tokens = operands.split("<", 1)[0].replace(",", " ").replace("#", " ").split()
        if not tokens:
            continue
        try:
            if int(tokens[-1], 16) != 0:
                continue
        except ValueError:
            continue
        if function in OPTIONAL_HOOK_CALLERS:
            continue
        bad.append(function + ":  " + stripped)
    return bad


@pytest.mark.parametrize("sketch", _sketches())
def test_nothing_calls_address_zero(sketch):
    """No call may resolve to address 0.

    Address 0 on this part is `_start_v3f`, the V3F's reset vector, so a call
    that lands there does not fault -- the calling core quietly re-runs the
    OTHER core's startup. The part then lockup-resets several times a second,
    with no fault record, printing a clean boot banner every time.

    This is not hypothetical. `static String line;` inside a sketch's loop() is
    enough: GCC emits a call to __cxa_atexit to register the local static's
    destructor, and under -nostartfiles nothing provided it. The reference
    resolved to zero with no undefined symbol, no warning, and no entry in
    `nm -u`. cores/ch32h4/ch32h4_cxx.cpp exists to supply it and the handful of
    other C++ runtime hooks that fail the same way.

    Scanning the disassembly is the check that generalises: it catches the next
    such symbol too, whatever that turns out to be.
    """
    elf = SKETCHES / sketch / ".pio/build/ch32h417/firmware.elf"
    if not OBJDUMP.is_file() or not elf.is_file():
        pytest.skip("toolchain or firmware.elf missing")
    bad = _calls_to_zero(elf)
    assert not bad, (
        sketch + ": " + str(len(bad)) + " call(s) to address 0 -- see "
        "cores/ch32h4/ch32h4_cxx.cpp\n" + "\n".join(bad[:10]))
