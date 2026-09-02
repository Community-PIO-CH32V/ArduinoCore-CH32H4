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
SKETCHES = ROOT / "tests" / "sketches"

# Which sketch is on the chip right now. One image at a time, so a fixture for
# a different sketch has to put its own back -- see Board._ensure().
_flashed = None

PORT = os.environ.get("CH32_PORT", "COM7")

# The end of boot is the V5F reporting itself ready, not the V3F's "boot ok" --
# the V3F is only half the image, and stopping at its line would mean the
# banner never contains anything the second core said.
BOOT_SENTINEL = "V5F: runtime ready"
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


def build(sketch: str):
    r = subprocess.run(["pio", "run", "-d", str(SKETCHES / sketch)],
                       capture_output=True, text=True)
    if r.returncode != 0:
        tool_failure(f"the {sketch} sketch does not build:\n"
                     + r.stdout[-4000:] + r.stderr[-4000:])


_flash_count = 0


def flash(sketch: str):
    global _flash_count
    _flash_count += 1
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

    binary = SKETCHES / sketch / ".pio" / "build" / "ch32h417" / "firmware.bin"
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


_ser = None


def _serial():
    """One handle, opened once. Two fixtures sharing the port cannot each hold
    their own -- the second open fails, and on Windows it fails with a message
    about the port being in use that reads like the probe having gone away."""
    global _ser
    if _ser is None:
        try:
            _ser = serial.Serial(PORT, BAUD, timeout=0.2)
        except Exception as exc:
            tool_failure(f"cannot open {PORT}: {exc}. Override with $CH32_PORT.")
    return _ser


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
    """A board running one particular sketch.

    More than one sketch is under test, and the chip holds one image at a time,
    so a Board is bound to its sketch and re-flashes on demand. Every call
    checks first: if the other fixture used the board in between, this one puts
    its own firmware back before doing anything. That keeps the tests
    order-independent, which matters because pytest is free to run them in any
    order and `-k` routinely does.
    """

    def __init__(self, sketch: str):
        self.sketch = sketch
        self._banner = ""

    @property
    def banner(self) -> str:
        self._ensure()
        return self._banner

    @property
    def ser(self):
        self._ensure()
        return _serial()

    def _ensure(self):
        """Put this board's firmware back if something else displaced it."""
        global _flashed
        if _flashed == self.sketch:
            return
        build(self.sketch)
        flash(self.sketch)
        self._banner = _sync(self.sketch)
        _flashed = self.sketch

    def reboot(self, timeout: float = 5.0) -> str:
        """Reset, wait for the prompt again, and return the whole boot report.

        For tests that deliberately crash the board -- the fault handler resets
        by design -- so the ones after them still have a board to talk to. The
        text is returned rather than discarded because the boot report is where
        the V3F prints the reset cause and replays any fault record, and that
        is the only evidence a test has that the previous run ended cleanly."""
        self._ensure()
        ser = _serial()
        ser.reset_input_buffer()
        reset()
        deadline = time.time() + timeout
        seen = ""
        while time.time() < deadline:
            chunk = ser.read(ser.in_waiting or 1)
            if chunk:
                seen += chunk.decode(errors="replace")
            if seen.rstrip().endswith(">"):
                self._banner = seen
                return seen
        raise AssertionError(f"board did not come back after reset: {seen!r}")

    def command(self, line: str, timeout: float = 3.0) -> str:
        self._ensure()
        ser = _serial()

        # Drain, wait, drain again. reset_input_buffer() only discards
        # what has already arrived, and the trailing space of the
        # previous prompt is often still in flight -- it then lands at
        # the head of the first reply line, and a parser doing
        # startswith("millis=") silently finds nothing. That produced a
        # flake roughly one run in three, in whichever test happened to
        # run first after a reflash.
        ser.reset_input_buffer()
        time.sleep(0.02)
        ser.reset_input_buffer()

        ser.write((line + "\n").encode())
        ser.flush()
        deadline = time.time() + timeout
        out = ""
        while time.time() < deadline:
            chunk = ser.read(ser.in_waiting or 1)
            if chunk:
                out += chunk.decode(errors="replace")
                if out.rstrip().endswith(">"):
                    return out

        # No prompt inside the timeout, so the reply is truncated -- whatever
        # the sketch was still printing is missing. Say so IN the returned
        # text: a parser looking for one key then reports a missing key, and a
        # missing key is indistinguishable from a driver that answered
        # "failed". Marking it here is the difference between "the card
        # disagreed with itself" and "the board did not finish answering",
        # which are not the same bug and were confused once.
        #
        # Not raised, because test_fault's `crash` command is SUPPOSED to
        # never return a prompt.
        out += f"\n<<TRUNCATED: no prompt within {timeout:.1f}s>>\n"
        return out


def _sync(sketch: str) -> str:
    """Reset with the port open, and return everything up to the prompt."""
    ser = _serial()
    time.sleep(0.2)
    ser.reset_input_buffer()
    reset()

    deadline = time.time() + 5.0
    banner = ""
    while time.time() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            banner += chunk.decode(errors="replace")
        if BOOT_SENTINEL in banner:
            break
    else:
        pytest.exit(
            f"{sketch}: the board did not reach {BOOT_SENTINEL!r} within 5 s. "
            "Ending the session rather than letting every test wait out its "
            f"own timeout.\n--- what it did say ---\n{banner!r}",
            returncode=1)

    # Wait for the sketch's prompt before handing the board over. Without this
    # the first command() answers with whatever tail of the banner was still
    # arriving, which looks like the board ignoring the command.
    #
    # Generous, because setup() is allowed to take its time: the Ethernet
    # sketch waits up to fifteen seconds for a DHCP lease before it prints
    # anything. This returns as soon as the prompt arrives, so a fast sketch
    # pays nothing for the allowance.
    deadline = time.time() + 20.0
    while time.time() < deadline:
        chunk = ser.read(ser.in_waiting or 1)
        if chunk:
            banner += chunk.decode(errors="replace")
        if banner.rstrip().endswith(">"):
            break
    return banner


BOARD_FIXTURES = ("board", "sd_board", "fs_board", "ethernet_board",
                  "tls_board", "dualcore_board", "adc_board", "i2s_board")


def pytest_terminal_summary(terminalreporter):
    """Report how many times the board was reprogrammed.

    Four sketches means four flashes if the grouping above is working, and a
    dozen if it silently stops working -- which costs minutes and wears the
    part, while every test still passes. Nothing else would notice.
    """
    if _flash_count:
        terminalreporter.write_line(
            f"board reprogrammed {_flash_count} time(s)")


def pytest_collection_modifyitems(items):
    """Group tests by which sketch they need, so each is flashed once.

    pytest collects files alphabetically, which interleaves the four sketches
    -- coretest, dualcore, coretest, ethernet, coretest, sdtest, coretest --
    and every switch is an erase, a flash and a reset. That is around a dozen
    reflashes for a suite that needs four, it takes minutes, and it puts wear
    on the part for nothing.

    The order within a group is left exactly as collected; only the groups are
    gathered. `board` goes first because most tests want it, so the common case
    of running a subset never pays for a switch at all.
    """
    order = {name: i for i, name in enumerate(BOARD_FIXTURES)}

    def key(item):
        for name in BOARD_FIXTURES:
            if name in getattr(item, "fixturenames", ()):
                return order[name]
        return len(order)          # needs no board: leave it at the end

    items.sort(key=key)


@pytest.fixture(autouse=True)
def _firmware_in_place(request):
    """Re-flash, if another fixture displaced this test's sketch, BEFORE the
    test body starts.

    Board.command() would do it lazily anyway, but then the flash lands in the
    middle of whatever the test is doing -- and test_millis_matches_the_host_clock
    measures the board against the host's wall clock, so a ten-second reflash
    between its two readings made the board look like it had lost 9 seconds.
    Doing it here keeps every board-switching cost outside the measurement.
    """
    for name in BOARD_FIXTURES:
        if name in request.fixturenames:
            request.getfixturevalue(name)._ensure()
            break


@pytest.fixture(scope="session")
def board():
    """The general-purpose test sketch. Almost every test wants this one."""
    if serial is None:
        pytest.skip("pyserial is not installed")
    return Board("coretest")


@pytest.fixture(scope="session")
def ethernet_board():
    """The sketch with the TCP server, the TCP client and the UDP socket.

    A third image, and worth it for the same reason as the second: none of the
    Client/Server/UDP code can be exercised without a peer, and a loopback test
    would prove the API compiles rather than that a frame reached the wire.
    """
    if serial is None:
        pytest.skip("pyserial is not installed")
    return Board("ethernet")


@pytest.fixture(scope="session")
def sd_board():
    """The SD block-layer sketch.

    Needs a card wired to the SDMMC default mapping: CK on PC12, CMD on PD2,
    D0 on PC8. Its tests skip rather than fail without one -- an unwired bench
    is a missing precondition, not a broken driver.
    """
    if serial is None:
        pytest.skip("pyserial is not installed")
    return Board("sdtest")


@pytest.fixture(scope="session")
def fs_board():
    """The filesystem sketch: FatFs, the FS API and the classic SD shim.

    Separate from sd_board because that one deliberately has no filesystem in
    it -- nearly every way an SD card fails, fails in the block layer, and a
    FatFs on top would collapse all of them into one "mount failed".
    """
    if serial is None:
        pytest.skip("pyserial is not installed")
    return Board("sdfstest")


@pytest.fixture(scope="session")
def tls_board():
    """The sketch built with board_build.tls = mbedtls.

    Its own image because mbedtls is about 250 KB of flash: every other sketch
    would pay for it, and the AES and DRBG tests need it while nothing else
    does.
    """
    if serial is None:
        pytest.skip("pyserial is not installed")
    return Board("tlstest")


@pytest.fixture(scope="session")
def dualcore_board():
    """The sketch that runs setup1()/loop1() on the V3F.

    Separate from `board` because it is a different image, and worth the extra
    flash: everything it covers -- the second core running at all, the FIFO,
    the hardware semaphores -- was built blind and stayed broken for a long
    time precisely because nothing here exercised it.
    """
    if serial is None:
        pytest.skip("pyserial is not installed")
    return Board("dualcore")


@pytest.fixture(scope="session")
def adc_board():
    """The timer-paced ADC sketch.

    Its own image because ADCInput claims TIM3 and DMA1 channel 7 for the
    whole run, and coretest's PWM and tone tests contend for the same timers.
    """
    if serial is None:
        pytest.skip("pyserial is not installed")
    return Board("adctest")


class Reply(dict):
    """A parsed key=value reply that explains itself when a key is missing.

    The raw text is kept because the interesting case is a reply that was cut
    short: a KeyError names the key that was wanted and says nothing about the
    board having stopped mid-sentence, and the two look identical from the
    traceback.
    """

    def __init__(self, raw, pairs):
        super().__init__(pairs)
        self.raw = raw

    def __missing__(self, key):
        raise AssertionError(
            f"the board's reply has no {key!r}. Reply was:\n{self.raw}")


@pytest.fixture(scope="session")
def i2s_board():
    """The I2S sketch.

    Everything it is asked to do here is silent. There is an amplifier and a
    speaker on instance 0's pins, and none of what these tests measure depends
    on the sample values -- the peripheral clocks zeros exactly as it clocks
    music.
    """
    if serial is None:
        pytest.skip("pyserial is not installed")
    return Board("i2stest")
