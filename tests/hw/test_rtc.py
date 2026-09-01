"""The real-time clock, and its integration with the C library.

The integration is the point. FatFs stamps files through get_fattime(), and
mbedtls checks certificate validity through time() -- neither of them knows an
RTC exists, and neither should have to. So these tests go through
gettimeofday() and time() rather than the driver, because that is the path
everything else actually uses.

Tests that switch the clock source are destructive to the time on the clock:
RTCSEL cannot be changed without resetting the backup domain, which clears the
counter. They put it back on the LSE afterwards.
"""
import time

import pytest

SRC_LSE, SRC_LSI, SRC_HSE = 1, 2, 3


def _kv(out):
    d = {}
    for line in out.splitlines():
        for part in line.strip().split():
            if "=" in part:
                k, _, v = part.partition("=")
                d[k.strip()] = v.strip()
    return d


@pytest.fixture(scope="module")
def rtc(board):
    """The clock running on the LSE, or a skip if there is no crystal."""
    d = _kv(board.command("rtcstart lse", timeout=10.0))
    if d.get("rtc_begin") != "1":
        pytest.skip("the LSE would not start -- is the 32 kHz crystal fitted?")
    return board


def test_the_lse_is_selected_and_at_the_right_rate(rtc):
    d = _kv(rtc.command("rtcstart lse", timeout=10.0))
    assert d["rtc_source"] == str(SRC_LSE), d
    assert d["rtc_hz"] == "32768", ("a watch crystal is 32768 Hz; anything "
                                    "else means the wrong divisor", d)


def test_setting_the_time_goes_through_the_c_library(rtc):
    """settimeofday() in, gettimeofday() and time() out.

    Both are checked because they are different newlib entry points and a core
    can easily supply one and not the other -- time() would then return
    (time_t)-1 while gettimeofday() looked perfect.
    """
    now = int(time.time())
    d = _kv(rtc.command(f"rtcset {now}", timeout=10.0))
    assert d["rtc_set"] == "1", d

    d = _kv(rtc.command("rtcget", timeout=10.0))
    assert d["rtc_gettimeofday_rc"] == "0", d
    assert abs(int(d["rtc_unix"]) - now) <= 2, d
    assert abs(int(d["rtc_time_t"]) - now) <= 2, ("time() disagrees with "
                                                  "gettimeofday()", d)
    assert 0 <= int(d["rtc_usec"]) < 1000000, d


def test_the_broken_down_time_is_right(rtc):
    """gmtime_r and strftime over the value, so the epoch conversion is
    checked end to end rather than as a bare number."""
    now = int(time.time())
    rtc.command(f"rtcset {now}", timeout=10.0)
    d = _kv(rtc.command("rtcget", timeout=10.0))
    want = time.strftime("%Y-%m-%dT%H:%M:", time.gmtime(now))
    assert d["rtc_iso"].startswith(want), (d.get("rtc_iso"), want)


def test_the_clock_advances_against_the_host(rtc):
    """Six seconds by the host's clock must be six by the board's.

    Measured against the HOST, because a clock compared only with itself
    advances perfectly at any rate at all.
    """
    now = int(time.time())
    rtc.command(f"rtcset {now}", timeout=10.0)
    t0 = time.time()
    time.sleep(6.0)
    d = _kv(rtc.command("rtcget", timeout=10.0))
    elapsed_host = time.time() - t0
    elapsed_board = (int(d["rtc_unix"]) + int(d["rtc_usec"]) / 1e6) - now
    # Serial round trips put a few milliseconds of noise on this, so the bound
    # is loose. A crystal that is out by enough to matter is out by percent.
    assert abs(elapsed_board - elapsed_host) < 0.25, (
        f"board {elapsed_board:.3f}s vs host {elapsed_host:.3f}s", d)


def test_an_unset_clock_says_so(rtc):
    """is_set has to distinguish "running" from "correct".

    Code that validates a certificate needs that distinction, and a counter
    ticking up from its epoch looks exactly like a working clock otherwise.
    Switching to a different source resets the counter, which is the only way
    to get back to an unset clock without a power cycle.
    """
    try:
        d = _kv(rtc.command("rtcstart lsi", timeout=10.0))
        assert d["rtc_begin"] == "1", d
        assert d["rtc_is_set"] == "0", ("a freshly reset counter must not "
                                        "claim to hold a real time", d)
        d = _kv(rtc.command("rtcget", timeout=10.0))
        assert d["rtc_iso"].startswith("2000-01-01"), (
            "an unset counter reads as the epoch", d)
    finally:
        rtc.command("rtcstart lse", timeout=10.0)
        rtc.command(f"rtcset {int(time.time())}", timeout=10.0)


@pytest.mark.parametrize("name,src,hz", [
    ("lsi", SRC_LSI, 40000),
    ("hse", SRC_HSE, 48828),
])
def test_the_other_clock_sources_start(rtc, name, src, hz):
    """All three sources have to work, because the choice is the sketch's.

    LSI needs no crystal but is only specified to 25-60 kHz -- it measures
    about 41.3 kHz here against a nominal 40000 divisor, which is roughly 3%,
    or minutes a day. HSE/512 is 48828.125 Hz, and the nearest whole divisor
    is 2.6 ppm short. Both are documented in ch32h4_rtc.c; this only checks
    they select and tick.
    """
    try:
        d = _kv(rtc.command(f"rtcstart {name}", timeout=10.0))
        assert d["rtc_begin"] == "1", d
        assert d["rtc_source"] == str(src), d
        assert d["rtc_hz"] == str(hz), d

        before = _kv(rtc.command("rtcget", timeout=10.0))
        time.sleep(2.5)
        after = _kv(rtc.command("rtcget", timeout=10.0))
        assert int(after["rtc_ticks"]) > int(before["rtc_ticks"]), (
            "the counter is not advancing", before, after)
    finally:
        rtc.command("rtcstart lse", timeout=10.0)
        rtc.command(f"rtcset {int(time.time())}", timeout=10.0)


def test_reselecting_the_running_source_keeps_the_time(rtc):
    """rtcstart on the source already running must NOT reset the counter.

    This is the warm-reboot case: the clock has been keeping time since before
    the firmware started, and a begin() that blindly reset the backup domain
    would throw it away every time the board came up.
    """
    now = int(time.time())
    rtc.command(f"rtcset {now}", timeout=10.0)
    d = _kv(rtc.command("rtcstart lse", timeout=10.0))
    assert d["rtc_is_set"] == "1", ("re-selecting the running source reset "
                                    "the clock", d)
    d = _kv(rtc.command("rtcget", timeout=10.0))
    assert abs(int(d["rtc_unix"]) - now) <= 3, d
