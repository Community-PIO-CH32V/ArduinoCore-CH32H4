"""Streaming audio out of the two internal DACs.

NOTHING HERE MAKES A SOUND. Every test writes mid-scale or measures timing;
there is an amplifier and a speaker on this bench and none of these reach it.

The sample clock is the thing worth measuring most carefully, because it is the
one number a listener would notice being wrong and the one a compile cannot
check: the DMA consumes exactly one word per conversion, so watching its
counter advance over a known interval measures the rate the hardware is really
running at.

Not covered, because the bench cannot: whether the output SOUNDS right, which
needs ears or a scope, and the RC reconstruction filter the pins want, which is
external. What is covered is that the conversions happen at the requested rate,
that both channels move together, and that a starved ring is noticed rather
than quietly repeating itself.
"""
import pytest

from conftest import kv


@pytest.fixture(scope="module")
def dac(dac_board):
    return dac_board


# The measurement window must be shorter than one pass of the ring, or `moved`
# wraps and the rate reads low -- which looks exactly like a timer bug. The
# default ring is 4096 frames, so each window below is well inside one pass.
RATE_WINDOWS = [(8000, 200), (44100, 40), (96000, 20)]


@pytest.mark.parametrize("rate,window_ms", RATE_WINDOWS)
def test_the_sample_clock_is_the_requested_rate(dac, rate, window_ms):
    """Within 1%. A timer clocked from SystemCoreClock rather than HCLK comes
    out exactly 4x wrong on this part, which is the mistake this catches."""
    r = kv(dac.command("rate %d" % rate, timeout=20))
    assert r["begun"] == 1, r.raw
    m = kv(dac.command("measure %d" % window_ms, timeout=20))
    measured = m["measured_hz"]
    assert m["moved"] < int(r["frames"]), (
        "the window covered a whole pass of the ring, so the count wrapped: "
        "%s" % m.raw)
    assert abs(measured - rate) / rate < 0.01, (
        "asked for %d Hz, measured %d Hz (moved %s frames in %s us)"
        % (rate, measured, m["moved"], m["elapsed_us"]))


def test_begin_refuses_rates_outside_the_range(dac):
    assert kv(dac.command("rate 999", timeout=20))["begun"] == 0
    assert kv(dac.command("rate 192001", timeout=20))["begun"] == 0
    assert kv(dac.command("rate 44100", timeout=20))["begun"] == 1


def test_a_second_begin_without_end_is_refused(dac):
    assert kv(dac.command("rate 44100", timeout=20))["begun"] == 1
    assert kv(dac.command("beginagain", timeout=20))["begun"] == 0


def test_end_releases_the_timer_so_begin_works_again(dac):
    dac.command("stop", timeout=20)
    r = kv(dac.command("timerowner", timeout=10))
    assert r["owner"] == "free", r.raw
    assert kv(dac.command("rate 44100", timeout=20))["begun"] == 1
    r = kv(dac.command("timerowner", timeout=10))
    assert r["owner"] == "audio", r.raw


def test_end_stops_the_timer_and_not_just_the_claim(dac):
    """Releasing the claim is bookkeeping. A TIM6 left counting keeps raising
    TRGO into whatever configures the DAC next, which is a fault in someone
    else's driver and very hard to trace back to here."""
    assert kv(dac.command("rate 44100", timeout=20))["begun"] == 1
    assert kv(dac.command("timerowner", timeout=10))["tim6_enabled"] == 1
    dac.command("stop", timeout=20)
    r = kv(dac.command("timerowner", timeout=10))
    assert r["tim6_enabled"] == 0, r.raw


def test_the_ring_starts_empty(dac):
    """Nearly all of it free right after begin(). One frame is held back so a
    full ring stays distinguishable from an empty one, hence the -1."""
    r = kv(dac.command("rate 8000", timeout=20))
    frames = r["frames"]
    avail = kv(dac.command("avail", timeout=10))["avail"]
    assert avail == frames - 1, (
        "expected %d free of a %d-frame ring, got %d" % (frames - 1, frames, avail))


def test_free_space_recovers_as_the_dma_drains(dac):
    """The other half of the accounting. A writer-only test passes on an
    implementation whose free count only ever falls."""
    dac.command("rate 8000", timeout=20)
    r = kv(dac.command("drain 400", timeout=20))
    assert r["free_after"] > r["free_before"], (
        "the DMA played for 20 ms and freed nothing: %s" % r.raw)


def test_available_frames_falls_as_frames_are_queued(dac):
    dac.command("rate 8000", timeout=20)
    before = kv(dac.command("avail", timeout=10))["avail"]
    kv(dac.command("fill 500", timeout=20))
    after = kv(dac.command("avail", timeout=10))["avail"]
    assert after < before, (
        "queued 500 frames and the free count went %s -> %s" % (before, after))


def test_underruns_stay_zero_while_the_ring_is_fed(dac):
    """The negative half. A counter that only ever reads zero proves nothing,
    which is why the starved case below exists too."""
    dac.command("rate 8000", timeout=20)
    r = kv(dac.command("feed 300", timeout=30))
    assert r["underruns"] == 0, r.raw


def test_underruns_are_counted_when_the_producer_stops(dac):
    """Stop writing for several ring-lengths and the DMA runs dry.

    Detected from the clock, not the ring pointers: the moment the read pointer
    laps the write pointer their difference wraps, so an empty ring and a full
    one read identically a few microseconds apart. At 8 kHz a 4096-frame ring
    is half a second, so 1500 ms is comfortably past dry.
    """
    dac.command("rate 8000", timeout=20)
    dac.command("fill 100", timeout=20)
    r = kv(dac.command("starve 1500", timeout=30))
    assert r["underruns"] > 0, "the DMA ran dry and nothing counted it"


# The sketch writes left=8192, right=-8192 throughout the ring. Converted to
# unsigned 12-bit -- (s + 32768) >> 4 -- those are 2560 and 1536, and silence
# is 2048. All three are distinct, so the register content says exactly what
# happened rather than merely being self-consistent.
CODE_LEFT = 2560
CODE_RIGHT = 1536
CODE_SILENCE = 2048


def test_mono_duplicates_the_left_sample_into_both_channels(dac):
    """Read the dual holding register back.

    Asserting the exact code, not just that the halves agree: silence has
    matching halves too, and silence is precisely what a write that never
    reached the ring would leave behind.
    """
    dac.command("rate 8000", timeout=20)
    r = kv(dac.command("monocheck", timeout=20))
    assert r["left"] != CODE_SILENCE, (
        "the ring still held silence, so nothing was written: %s" % r.raw)
    assert r["left"] == CODE_LEFT, r.raw
    assert r["right"] == CODE_LEFT, (
        "mono did not duplicate left into the right channel: %s" % r.raw)


def test_stereo_keeps_the_channels_apart(dac):
    """The complement, and the reason the mono test means anything: with the
    same samples in stereo the two halves must differ. Without this, a
    writeFrames() that ignored the right channel entirely would pass the mono
    test and never be caught."""
    dac.command("rate 8000", timeout=20)
    r = kv(dac.command("stereocheck", timeout=20))
    assert r["left"] == CODE_LEFT, r.raw
    assert r["right"] == CODE_RIGHT, r.raw
