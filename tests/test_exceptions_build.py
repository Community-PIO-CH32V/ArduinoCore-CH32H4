import subprocess
import pathlib
import shutil
import pytest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SKETCH = ROOT / "tests" / "sketches" / "exceptions"

pytestmark = pytest.mark.skipif(shutil.which("pio") is None,
                                reason="PlatformIO CLI not on PATH")


@pytest.mark.parametrize("setting", ["disabled", "enabled"])
def test_both_exception_settings_link(setting):
    r = subprocess.run(["pio", "run", "-d", str(SKETCH), "-e", f"exc_{setting}"],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout[-4000:] + r.stderr[-4000:]


def test_enabling_exceptions_emits_unwind_tables():
    """-fexceptions without .eh_frame would link and then fail at run time."""
    size = pathlib.Path.home() / ".platformio/packages/toolchain-riscv/bin/riscv-wch-elf-size.exe"
    if not size.is_file():
        pytest.skip("toolchain not installed")
    elf = SKETCH / ".pio/build/exc_enabled/firmware.elf"
    if not elf.is_file():
        pytest.skip("run test_both_exception_settings_link first")
    out = subprocess.run([str(size), "-A", str(elf)],
                         capture_output=True, text=True, check=True).stdout
    for line in out.splitlines():
        if line.startswith(".eh_frame"):
            assert int(line.split()[1]) > 1000, line
            return
    raise AssertionError(".eh_frame missing from the exceptions build")
