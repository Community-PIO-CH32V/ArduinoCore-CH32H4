"""The IWDG watchdog and clock-gated sleep, through Adafruit_SleepyDog.

The bite test deliberately resets the board. That is the only way to prove a
watchdog: one that is configured but never actually fires is indistinguishable
from one that works, right up until the day something hangs. The sketch arms
nothing at boot, so the board comes back usable afterwards -- which matters,
because the IWDG has no off switch and a sketch that armed it in setup() would
leave the board resetting on a cadence while the NEXT sketch was being flashed.
"""
import re
import time

import pytest

from conftest import Reply, _serial, _sync


def kv(text):
    out = {}
    for line in text.splitlines():
        m = re.match(r"^([a-z0-9_]+)=(-?\d+)$", line.strip())
        if m:
            out[m.group(1)] = int(m.group(2))
    return Reply(text, out)


@pytest.mark.parametrize("ms", [50, 250])
def test_sleep_returns_after_about_the_right_time(dog_board, ms):
    """sleep() must RETURN, and to the same program.

    That is the whole reason it gates the core clock instead of entering Stop:
    Stop needs both cores, and its only timed wake on this part is a reset.
    """
    d = kv(dog_board.command("dogsleep %d" % ms, timeout=10))
    assert d["dog_slept"] >= ms, d.raw
    # A tick of slack either way; the wake is the millisecond SysTick.
    assert ms <= d["dog_wall"] <= ms + 40, d.raw


def test_arming_reports_what_it_programmed(dog_board):
    """The LSI is specified at 25-60 kHz, so the timeout is nominal.

    What must be exact is the bookkeeping: enable() returns what it programmed
    from the nominal figure, and isEnabled() then says so -- the hardware has
    no bit to ask.
    """
    d = kv(dog_board.command("dogarm 4000", timeout=10))
    assert d["dog_programmed"] > 0, d.raw
    assert d["dog_enabled"] == 1, d.raw
    dog_board.command("dogfeed", timeout=5)


def test_an_armed_watchdog_that_is_fed_does_not_bite(dog_board):
    """Two seconds of feeding against a 1 s timeout."""
    kv(dog_board.command("dogarm 1000", timeout=10))
    ser = _serial()
    ser.reset_input_buffer()
    for _ in range(20):
        dog_board.command("dogfeed", timeout=5)
        time.sleep(0.1)
    # A reset would have printed the core's banner.
    assert b"CH32H4 Arduino core" not in ser.read(ser.in_waiting or 1), (
        "the board reset while being fed every 100 ms")


def test_an_unfed_watchdog_resets_the_board(dog_board):
    """The test that says the watchdog is real.

    Timed loosely: the interval is the programmed timeout plus however long the
    core takes to boot and reach setup(), and the LSI's tolerance is a factor
    of two by datasheet. This asks whether it fires at all and roughly when,
    not how accurate it is.
    """
    # Written straight to the port rather than through command(): that waits
    # for the prompt, and the prompt never comes -- the sketch spins until the
    # watchdog shoots it. command() would sit through the reset and swallow the
    # very banner this test is looking for.
    dog_board._ensure()
    ser = _serial()
    ser.reset_input_buffer()
    ser.write(b"dogstarve 2000\n")
    ser.flush()

    deadline = time.time() + 8.0
    banner = b""
    while time.time() < deadline and b"dogtest ready" not in banner:
        banner += ser.read(ser.in_waiting or 1)

    assert b"dogtest ready" in banner, (
        "no reset within 8 s of starving a 2 s watchdog:\n"
        + banner.decode("utf-8", "replace")[-400:])

    # Back to a usable board, with nothing armed. _sync rather than a bare
    # reset: it waits for the boot banner AND the sketch's prompt, and a
    # command sent before the prompt is answered by nobody.
    _sync("dogtest")
    d = kv(dog_board.command("dogsleep 20", timeout=10))
    assert d["dog_slept"] >= 20, d.raw
