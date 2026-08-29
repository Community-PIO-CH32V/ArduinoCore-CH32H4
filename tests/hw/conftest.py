"""Hardware fixtures: build, flash, and talk to the board over its console.

Three things here are deliberate and were expensive to learn elsewhere.

1. ONE boot check for the whole session. When the board is dead, every test
   otherwise waits out its full timeout in turn, and a suite that runs in a
   minute takes ten to tell you the firmware does not boot.

2. A tool failure exits with a status DISTINCT from a firmware failure. wlink
   prints "Flash done" even when a 0x55 protocol error voided the write, and a
   wedged probe looks exactly like firmware that does not boot. In the libhal
   port that produced a dozen confident reproductions of a bug that did not
   exist.

3. wlink is pinned to 0.1.2. 0.1.1 reports "Probe is not attached to an MCU"
   on this part -- a version problem wearing the costume of a wiring problem.
"""
import os
import pathlib
import shutil
import subprocess
import sys
import time

import pytest

try:
    import serial
except ImportError:  # pragma: no cover
    serial = None

ROOT = pathlib.Path(__file__).resolve().parents[2]
SKETCH = ROOT / "tests" / "sketches" / "coretest"
BUILD = SKETCH / ".pio" / "build" / "ch32h417"

PORT = os.environ.get("CH32_PORT", "COM7")
BAUD = 115200

# Exit codes. The point is that a broken tool can never be read as a broken
# board.
EXIT_TOOL_FAILURE = 3


def _find_wlink() -> pathlib.Path | None:
    candidates = []
    if os.environ.get("WLINK"):
        candidates.append(pathlib.Path(os.environ["WLINK"]))
    candidates.append(ROOT / "tools" / "bin" / "wlink.exe")
    candidates.append(pathlib.Path.home()
                      / ".platformio/packages/tool-wlink/wlink.exe")
    for c in candidates:
        if c.is_file():
            return c
    found = shutil.which("wlink")
    return pathlib.Path(found) if found else None


def _wlink_version(exe: pathlib.Path) -> str:
    r = subprocess.run([str(exe), "--version"], capture_output=True, text=True)
    return (r.stdout + r.stderr).strip().split("\n")[0]


def tool_failure(message: str):
    """End the session with a status that cannot be mistaken for a firmware
    result."""
    print("\nTOOL FAILURE (not a firmware failure): " + message, file=sys.stderr)
    raise SystemExit(EXIT_TOOL_FAILURE)


def build():
    r = subprocess.run(["pio", "run", "-d", str(SKETCH)],
                       capture_output=True, text=True)
    if r.returncode != 0:
        tool_failure("the test sketch does not build:\n"
                     + r.stdout[-4000:] + r.stderr[-4000:])


def flash():
    exe = _find_wlink()
    if exe is None:
        tool_failure("wlink not found. Set $WLINK, or drop wlink.exe 0.1.2 in "
                     "tools/bin/. The PlatformIO package ships 0.1.1, which "
                     "reports 'Probe is not attached to an MCU' on this part.")

    version = _wlink_version(exe)
    if "0.1.2" not in version:
        tool_failure(f"wlink {version!r} at {exe}. This part needs 0.1.2: "
                     "0.1.1 reports 'Probe is not attached to an MCU', and the "
                     "0.1.2 x64 build fails with a driver error on Windows. "
                     "Use the x86 build.")

    binary = BUILD / "firmware.bin"
    if not binary.is_file():
        tool_failure(f"no {binary} -- did the build run?")

    # Erase first, with the same tool. An OpenOCD erase clears only the first
    # 448 KB, so anything a previous wlink write left past that survives and
    # the board boot-loops. Do not mix the two tools.
    for args in (["erase"], ["flash", "--address", "0x08000000", str(binary)]):
        r = subprocess.run([str(exe)] + args, capture_output=True, text=True)
        out = r.stdout + r.stderr
        # A 0x55 protocol error invalidates the operation before it, and wlink
        # still prints "Flash done". Both conditions must be checked.
        if "0x55" in out:
            tool_failure("wlink protocol error 0x55 -- the operation before it "
                         "never happened. Unplug the probe physically and "
                         "power-cycle the board.\n" + out[-2000:])
        if args[0] == "flash" and "Flash done" not in out:
            tool_failure("wlink did not report success:\n" + out[-2000:])


def reset():
    """Reset with the port already open.

    wlink resets the part at the end of `flash`, so the banner is already on
    the wire before anything opens the port -- the first fifty characters are
    simply lost, and what does arrive starts mid-byte and decodes as garbage.
    That reads exactly like a baud-rate bug in the firmware, which is what it
    was mistaken for once. Reset again, deliberately, once someone is
    listening.
    """
    exe = _find_wlink()
    r = subprocess.run([str(exe), "reset"], capture_output=True, text=True)
    out = r.stdout + r.stderr
    if "0x55" in out:
        tool_failure("wlink protocol error 0x55 on reset.\n" + out[-2000:])


class Board:
    def __init__(self, banner: str, ser):
        self.banner = banner
        self.ser = ser

    def command(self, line: str, timeout: float = 3.0) -> str:
        self.ser.reset_input_buffer()
        self.ser.write((line + "\n").encode())
        self.ser.flush()
        deadline = time.time() + timeout
        out = ""
        while time.time() < deadline:
            chunk = self.ser.read(self.ser.in_waiting or 1)
            if chunk:
                out += chunk.decode(errors="replace")
                if out.rstrip().endswith(">"):
                    break
        return out


@pytest.fixture(scope="session")
def board():
    if serial is None:
        pytest.skip("pyserial is not installed")

    build()
    flash()

    try:
        ser = serial.Serial(PORT, BAUD, timeout=0.2)
    except Exception as exc:
        tool_failure(f"cannot open {PORT}: {exc}. Override with $CH32_PORT.")

    # Now that someone is listening, reset again and catch the banner whole.
    time.sleep(0.2)
    ser.reset_input_buffer()
    reset()

    deadline = time.time() + 5.0
    banner = ""
    while time.time() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            banner += chunk.decode(errors="replace")
        if "boot ok" in banner:
            break
    else:
        pytest.exit(
            "the board did not reach 'boot ok' within 5 s. Ending the session "
            "rather than letting every test wait out its own timeout.\n"
            f"--- what it did say ---\n{banner!r}",
            returncode=1)

    return Board(banner, ser)
