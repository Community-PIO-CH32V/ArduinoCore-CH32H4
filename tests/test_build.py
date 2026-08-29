import subprocess
import pathlib
import shutil
import pytest

ROOT = pathlib.Path(__file__).resolve().parents[1]
SKETCH = ROOT / "tests" / "sketches" / "minimal"

pytestmark = pytest.mark.skipif(shutil.which("pio") is None,
                                reason="PlatformIO CLI not on PATH")


def test_minimal_sketch_builds():
    r = subprocess.run(["pio", "run", "-d", str(SKETCH)],
                       capture_output=True, text=True)
    assert r.returncode == 0, r.stdout[-6000:] + r.stderr[-6000:]
    assert (SKETCH / ".pio" / "build" / "ch32h417" / "firmware.elf").is_file()


def test_nano_specs_is_never_used():
    """nano.specs rewrites -lstdc++ to -lstdc++_nano, which has no .eh_frame,
    so every throw reaches std::terminate after a perfectly clean link."""
    r = subprocess.run(["pio", "run", "-d", str(SKETCH), "-v"],
                       capture_output=True, text=True)
    assert "nano.specs" not in r.stdout
