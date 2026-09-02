"""I2S transmit, on both of the part's I2S blocks.

Everything here runs SILENTLY. There is a real amplifier and a real speaker on
instance 0's pins, and none of what these tests measure -- the divider, the DMA
throughput, the underflow count -- depends on the sample values, because the
peripheral clocks a buffer of zeros exactly as it clocks a buffer of music. The
sketch's tone command takes an amplitude that defaults to zero for the same
reason. Nothing in this file passes a non-zero one.

Not covered, because the bench cannot: receive (no microphone), slave mode (no
external clock source), and whether the output SOUNDS right (needs ears or a
scope). What is covered is that the transmit path clocks data out at the rate
the divider claims, which is the part that can fail silently.
"""
import pytest

from conftest import Reply


def kv(out):
    d = {}
    for line in out.splitlines():
        for part in line.strip().split():
            if "=" in part:
                k, _, v = part.partition("=")
                d[k.strip()] = v.strip()
    return Reply(out, d)


def begin(i2s_board, rate, bits=16):
    return kv(i2s_board.command("i2sbegin %d %d" % (rate, bits), timeout=6))


def instance(i2s_board, id_, rx=0):
    return kv(i2s_board.command("i2sinst %d %d" % (id_, rx), timeout=6))


def silence(i2s_board, ms):
    """Clock zeros for ms milliseconds and report the measured rate."""
    return kv(i2s_board.command("i2ssilence %d" % ms, timeout=ms / 1000.0 + 6))


# ---- the divider ----------------------------------------------------------

def test_the_io_rail_is_high_enough(i2s_board):
    """Every I2S pin sits in the VIO18 domain, which powers up at 1.2 V. A
    3.3 V audio device sees no valid high at all until the core raises it."""
    d = kv(i2s_board.command("i2spins"))
    assert int(d["vio18_sel"]) >= 2


def test_forty_four_one_lands_within_three_cents(i2s_board):
    """The divider is 2*I2SDIV+ODD against a 400 MHz clock, so 44.1 kHz is
    approximated rather than hit. 1564 ppm is about three cents, which is the
    reason this part uses I2S rather than the SAI -- the SAI's 6-bit divider
    manages about twenty-one."""
    instance(i2s_board, 0)
    r = begin(i2s_board, 44100)
    assert r["i2s_begin"] == "1"
    assert abs(int(r["i2s_rate_err_ppm"])) < 2000


def test_forty_eight_kilohertz_is_exact(i2s_board):
    """400 MHz / (32 * 260) is not 48000, but 48 kHz divides the clock more
    kindly than 44.1 does; this pins whatever the hardware can actually do."""
    instance(i2s_board, 0)
    r = begin(i2s_board, 48000)
    assert r["i2s_begin"] == "1"
    assert abs(int(r["i2s_rate_err_ppm"])) < 5000


def test_rates_below_the_divider_floor_are_refused(i2s_board):
    """The largest division is 511 and it divides SYSCLK, so in 16-bit mode
    nothing below about 24.5 kHz is reachable. Returning false beats clocking a
    rate nobody asked for: a sketch playing 8 kHz audio at 24.5 kHz is a bug
    that sounds like a bug and gets blamed on the file."""
    instance(i2s_board, 0)
    floor = int(begin(i2s_board, 44100)["i2s_rate_min"])
    assert 20000 < floor < 30000
    for rate in (8000, 16000, 22050):
        assert rate < floor
        assert begin(i2s_board, rate)["i2s_begin"] == "0", \
            "a rate below the divider floor was accepted"


def test_thirty_two_bit_frames_halve_the_floor(i2s_board):
    """A 32-bit frame is 64 bus clocks against 16-bit's 32, so the same divider
    range reaches half as low."""
    instance(i2s_board, 0)
    floor16 = int(begin(i2s_board, 44100, 16)["i2s_rate_min"])
    floor32 = int(begin(i2s_board, 44100, 32)["i2s_rate_min"])
    assert abs(floor32 * 2 - floor16) < floor16 * 0.05


# ---- the transmit path ----------------------------------------------------

def test_the_dma_clocks_data_at_the_divider_rate(i2s_board):
    """Measured from outside, against millis(), over long enough that the ring
    buffer's contents do not dominate.

    This is the test that the whole path runs: the divider, the peripheral, the
    DMA, the half-complete interrupt and the ring. If any of them stalls the
    measured rate falls; if the clock is wrong it moves."""
    instance(i2s_board, 0)
    r = begin(i2s_board, 44100)
    actual = int(r["i2s_rate_actual"])
    m = silence(i2s_board, 2000)
    assert m["i2s_underflows"] == "0"
    assert abs(int(m["i2s_measured_rate"]) - actual) < actual * 0.01


def test_a_long_run_does_not_underflow(i2s_board):
    """Four seconds is about 2700 DMA half-complete interrupts. An underflow
    means the sketch could not keep the ring fed, and at 44.1 kHz stereo with
    nothing else running there is no excuse for one."""
    instance(i2s_board, 0)
    assert begin(i2s_board, 44100)["i2s_begin"] == "1"
    m = silence(i2s_board, 4000)
    assert m["i2s_underflows"] == "0"


def test_end_and_restart(i2s_board):
    """end() has to release the DMA channel and the peripheral, or the second
    begin() half-configures a block that is already running."""
    instance(i2s_board, 0)
    assert begin(i2s_board, 44100)["i2s_begin"] == "1"
    assert kv(i2s_board.command("i2send"))["i2s_end"] == "1"
    assert begin(i2s_board, 48000)["i2s_begin"] == "1"
    m = silence(i2s_board, 1000)
    assert m["i2s_underflows"] == "0"


# ---- the two instances ----------------------------------------------------

def test_the_two_instances_have_different_pins(i2s_board):
    """The bug this pins down was silent.

    There are two I2S blocks -- the audio halves of SPI2 and SPI3 -- and the
    pin setup hardcoded the first one's three pins, on GPIOB, with a single
    alternate function. Constructing the second instance therefore clocked SPI3
    while driving SPI2's pads: the peripheral ran, the DMA ran, no call
    returned an error, and the wrong three pins toggled.

    The alternate function is per PIN as well. Instance 0 is AF5 on all three,
    which is why one constant worked; instance 1 is AF6 on clock and AF7 on
    data."""
    a = instance(i2s_board, 0)
    b = instance(i2s_board, 1)

    assert a["i2s_pin_ck"] != b["i2s_pin_ck"]
    assert a["i2s_pin_ws"] != b["i2s_pin_ws"]
    assert a["i2s_pin_sd"] != b["i2s_pin_sd"]
    assert b["i2s_af_ck"] != b["i2s_af_sd"], \
        "instance 1 uses one AF for clock and data, which the mux does not"


@pytest.mark.parametrize("id_", [0, 1])
def test_an_instance_takes_its_own_pins_and_refuses_the_others(i2s_board, id_):
    """setBCLK() checks rather than chooses -- the mux fixes these pins. The
    observable half is that it has to check against the pins of the instance it
    belongs to, not against instance 0's."""
    r = instance(i2s_board, id_)
    assert r["i2s_accepts_own_ck"] == "1"
    assert r["i2s_accepts_other_ck"] == "0"


@pytest.mark.parametrize("id_", [0, 1])
def test_either_instance_clocks_at_the_right_rate(i2s_board, id_):
    """Instance 1 is not wired to anything on this board, which does not stop
    it from being measurable: the DMA feeds the peripheral and the peripheral
    clocks its pins whether or not a device is listening."""
    instance(i2s_board, id_)
    r = begin(i2s_board, 44100)
    assert r["i2s_begin"] == "1"
    actual = int(r["i2s_rate_actual"])
    m = silence(i2s_board, 2000)
    assert m["i2s_underflows"] == "0"
    assert abs(int(m["i2s_measured_rate"]) - actual) < actual * 0.01


def test_the_measured_rate_subtracts_what_is_still_queued(i2s_board):
    """Frames written include a whole bufferful the wire has never seen, and
    over a short run that reads as a rate several percent high -- which looks
    exactly like a clocking bug and got the divider blamed once.

    A short run and a long one have to agree, which they only do if the queued
    frames are subtracted."""
    instance(i2s_board, 0)
    actual = int(begin(i2s_board, 44100)["i2s_rate_actual"])
    short = int(silence(i2s_board, 500)["i2s_measured_rate"])
    long_ = int(silence(i2s_board, 3000)["i2s_measured_rate"])
    assert abs(short - actual) < actual * 0.02
    assert abs(long_ - actual) < actual * 0.01
    assert abs(short - long_) < actual * 0.02
