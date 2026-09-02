"""Timer-paced ADC capture, and the two internal channels.

The bench has no signal generator and no free wiring, so the known inputs are
made rather than found:

  * PC0 and PC1 (A0 and A1) go nowhere on this board, and each can be driven
    from its own GPIO output driver while the ADC samples the pad. That gives
    two inputs at known, opposite levels -- which is what makes channel ORDER
    checkable. A floating pin cannot do it: it reads whatever the
    sample-and-hold last contained, which is exactly what a channel read off
    the wrong pin also reads.

  * ATEMP and AVREF are known by construction. AVREF in particular is a fixed
    1.20 V, so it doubles as the check that a mid-scale reading is real.
"""
import re

import pytest

from conftest import Reply


def kv(text: str) -> dict:
    """Parse the sketch's key=value replies.

    Values are stripped before conversion: a trailing prompt space from the
    previous command can still land at the head of a line, and int(" 8000")
    happens to work while startswith() does not.
    """
    out = {}
    for line in text.splitlines():
        m = re.match(r"^([a-z0-9_]+)=(-?\d+)$", line.strip())
        if m:
            out[m.group(1)] = int(m.group(2))
    return Reply(text, out)


def begin(adc_board, rate, spec):
    return kv(adc_board.command("adcbegin %d %s" % (rate, spec), timeout=5))


def capture(adc_board, ms, nchannels):
    return kv(adc_board.command("adccapture %d %d" % (ms, nchannels),
                                timeout=ms / 1000.0 + 5))


def single(adc_board):
    """The single-shot readings, with any paced capture stopped first.

    ADCInput owns the ADC's regular sequence while it runs, and analogRead()
    refuses rather than converting against somebody else's scan. Tests share
    one board across a session, so a capture left running by an earlier test
    would make every reading here zero.
    """
    adc_board.command("adcend")
    r = kv(adc_board.command("adcvref"))
    assert r["adc_capturing"] == 0
    return r


# ---- the timer ------------------------------------------------------------

@pytest.mark.parametrize("rate", [1000, 8000, 44100, 100000])
def test_the_timer_hits_the_requested_rate(adc_board, rate):
    """The prescaler and reload are integers, so not every rate is reachable
    exactly -- but these are, and a rate that is merely close usually means the
    divider was computed against the wrong clock."""
    r = begin(adc_board, rate, "0")
    assert r["adc_begin"] == 1
    assert abs(r["adc_rate_err_ppm"]) < 1000


def test_capture_delivers_at_the_rate_the_timer_claims(adc_board):
    """Measured against the host's clock, not against the timer producing it.

    This is the test that the whole chain runs: timer TRGO, the ADC trigger,
    the DMA, the half-complete interrupt and the ring. Any of them stalling
    shows up here as a throughput below the rate."""
    assert begin(adc_board, 8000, "0")["adc_begin"] == 1
    r = capture(adc_board, 1000, 1)
    assert r["adc_overflows"] == 0
    assert abs(r["adc_samples_per_sec"] - 8000) < 8000 * 0.02


def test_a_hundred_thousand_samples_a_second(adc_board):
    """Well above audio rates, and the point at which the interrupt rate starts
    to matter: at 100 kHz the DMA half-complete fires every 320 us."""
    assert begin(adc_board, 100000, "0")["adc_begin"] == 1
    r = capture(adc_board, 500, 1)
    assert r["adc_overflows"] == 0
    assert abs(r["adc_samples_per_sec"] - 100000) < 100000 * 0.02


# ---- channel order --------------------------------------------------------

@pytest.mark.parametrize("a0,a1", [(0, 1), (1, 0)])
def test_a_two_pin_scan_keeps_its_channels_in_order(adc_board, a0, a1):
    """Both polarities, because a swap and a correct capture are the same
    reading with the levels exchanged. Running it each way makes them
    distinguishable.

    The pins are driven AFTER begin(), because begin() puts every pin it
    samples into analog mode and would undo the output driver."""
    assert begin(adc_board, 8000, "01")["adc_begin"] == 1
    adc_board.command("adcdrive 0 %d" % a0)
    adc_board.command("adcdrive 1 %d" % a1)
    r = capture(adc_board, 500, 2)

    assert r["adc_overflows"] == 0
    # Whole scans only. A partial one shifts every later sample onto the wrong
    # channel, and would make the assertions below fail for a reason that has
    # nothing to do with ordering.
    assert r["adc_partial_scan"] == 0
    assert r["adc_ch0_n"] == r["adc_ch1_n"]

    lo, hi = (0, 1) if a0 == 0 else (1, 0)
    assert r["adc_ch%d_max" % lo] < 64, "the pin driven LOW is not near zero"
    assert r["adc_ch%d_min" % hi] > 4000, \
        "the pin driven HIGH is not near full scale"


def test_the_scan_rate_is_per_scan_not_per_sample(adc_board):
    """Two channels at 8 kHz is 16000 samples a second, not 8000. Getting this
    backwards is the obvious way to read the API, so it is worth pinning."""
    assert begin(adc_board, 8000, "01")["adc_begin"] == 1
    r = capture(adc_board, 500, 2)
    assert abs(r["adc_scans_per_sec"] - 8000) < 8000 * 0.02
    assert abs(r["adc_samples_per_sec"] - 16000) < 16000 * 0.02


# ---- the internal channels ------------------------------------------------

def test_the_internal_channels_are_not_pins(adc_board):
    """ATEMP and AVREF are numbered above PINS_COUNT deliberately, so anything
    treating them as real pins indexes past the end of the pin table instead of
    quietly reading pin 0."""
    r = single(adc_board)
    assert r["pin_atemp"] == 96
    assert r["pin_avref"] == 97


def test_the_internal_reference_measures_vdda(adc_board):
    """A nominal 1.20 V against a 3.3 V supply is 1489 of 4095 counts. The
    board is USB-powered, so a few percent either way is the supply, not the
    ADC -- but a reading far from mid-scale means the reference channel is not
    what is being converted."""
    r = single(adc_board)
    assert 3000 < r["vdda_volts_x1000"] < 3600
    # analogRead defaults to 10-bit, so the raw figure is a quarter of the
    # 12-bit conversion.
    assert 300 < r["avref_raw"] < 450


def test_the_die_temperature_is_plausible(adc_board):
    """Not an accuracy test -- the sensor carries the error of both VDDA and a
    single-point factory calibration, and it measures the die rather than the
    room. It does catch a calibration word that failed to read, which returns
    zero, and a sign error, which puts it far below ambient."""
    r = single(adc_board)
    assert 1000 < r["temp_c_x100"] < 7000, "die temperature is not plausible"


def test_an_internal_channel_can_be_captured_by_the_dma(adc_board):
    """The same value through the paced path as through analogRead(), which is
    the check that the scan is converting the channel it was given."""
    reference = single(adc_board)
    assert begin(adc_board, 1000, "tv")["adc_begin"] == 1
    r = capture(adc_board, 500, 2)
    assert r["adc_overflows"] == 0
    # ch1 is AVREF. analogRead's figure is 10-bit; the captured one is 12-bit.
    assert abs(r["adc_ch1_mean"] - reference["avref_raw"] * 4) < 64


def test_an_internal_channel_gets_its_long_sample_window(adc_board):
    """The failure this catches is specific and quiet.

    Both internal channels are driven through a high impedance. Sampled with
    the short window the pins use, they return whatever the sample-and-hold
    last held -- which in a scan is the PREVIOUS channel's value. So a scan of
    [A0 driven to zero, ATEMP] would report a temperature channel reading near
    zero, tracking A0, and looking like a plausible cold reading rather than
    like a bug.

    Driving A0 to zero and requiring ATEMP to stay near its own value is what
    separates the two."""
    reference = single(adc_board)["atemp_raw"] * 4

    assert begin(adc_board, 8000, "0t")["adc_begin"] == 1
    adc_board.command("adcdrive 0 0")
    r = capture(adc_board, 500, 2)

    assert r["adc_ch0_max"] < 64, "A0 is not being driven low"
    assert r["adc_ch1_min"] > 512, \
        "ATEMP is tracking the channel before it -- short sample window"
    assert abs(r["adc_ch1_mean"] - reference) < 128


def test_a_rate_the_channel_list_cannot_convert_is_refused(adc_board):
    """The internal channels need the slowest sample window, about 20 us each,
    so two of them cap the scan rate near 24.8 kHz. Asking for more has to fail
    at begin(): an over-triggered ADC does not slow down, it drops the trigger,
    and one dropped scan puts every sample after it on the wrong channel for
    the rest of the capture."""
    r = begin(adc_board, 30000, "tv")
    assert r["adc_rate_max"] < 30000
    assert r["adc_begin"] == 0, "a rate above the ceiling was accepted"

    # And the same list just under the ceiling still starts, so the check is a
    # ceiling and not a blanket refusal of internal channels.
    assert begin(adc_board, 20000, "tv")["adc_begin"] == 1


def test_pins_are_faster_than_internal_channels(adc_board):
    """A pin uses the short window and an internal channel the long one, so the
    ceiling depends on the channel LIST and not merely its length."""
    two_pins = begin(adc_board, 8000, "01")["adc_rate_max"]
    two_internal = begin(adc_board, 1000, "tv")["adc_rate_max"]
    assert two_pins > two_internal * 4


def test_the_capture_stops_and_can_be_restarted(adc_board):
    """end() has to release TIM3 and the DMA channel, or the second begin()
    finds the timer already claimed. The SD driver had exactly this bug and it
    presented as a dead peripheral rather than as a failed claim."""
    assert begin(adc_board, 8000, "0")["adc_begin"] == 1
    assert kv(adc_board.command("adcend"))["adc_end"] == 1
    assert begin(adc_board, 16000, "0")["adc_begin"] == 1
    r = capture(adc_board, 300, 1)
    assert r["adc_overflows"] == 0
    assert abs(r["adc_samples_per_sec"] - 16000) < 16000 * 0.03


# ---- resolution -----------------------------------------------------------

def test_analog_read_defaults_to_ten_bits(adc_board):
    """Arduino's default, and the reason a fresh sketch reading the 1.20 V
    reference sees about 376 rather than about 1504."""
    adc_board.command("adcend")
    r = kv(adc_board.command("adcres 10"))
    assert r["adc_res_bits"] == 10
    assert 300 < r["avref_at_res"] < 450


def test_twelve_bits_gives_the_converter_its_full_range(adc_board):
    """What the part actually converts. The same input has to read four times
    higher than at 10 bits -- not merely higher, which a gain change would also
    do."""
    adc_board.command("adcend")
    ten = kv(adc_board.command("adcres 10"))["avref_at_res"]
    twelve = kv(adc_board.command("adcres 12"))["avref_at_res"]
    assert kv(adc_board.command("adcres 12"))["adc_res_bits"] == 12
    assert 1200 < twelve < 1800
    assert abs(twelve - ten * 4) < 32


@pytest.mark.parametrize("bits,scale", [(8, 0.25), (10, 1.0), (12, 4.0), (16, 64.0)])
def test_every_resolution_scales_from_the_same_conversion(adc_board, bits, scale):
    """Above 12 the extra bits are zeros -- the value is scaled, not
    interpolated. Below 12 the low bits are dropped. Either way the reading
    tracks a fixed 1.20 V input by exactly the power of two."""
    adc_board.command("adcend")
    r = kv(adc_board.command("adcres %d" % bits))
    assert r["adc_res_bits"] == bits
    expected = 376 * scale
    assert abs(r["avref_at_res"] - expected) < max(16, expected * 0.06)


def test_a_rejected_resolution_leaves_the_previous_one(adc_board):
    """Clamping would silently read at a width the sketch did not choose."""
    adc_board.command("adcend")
    adc_board.command("adcres 12")
    assert kv(adc_board.command("adcres 99"))["adc_res_bits"] == 12
    assert kv(adc_board.command("adcres 0"))["adc_res_bits"] == 12
    # Put it back, so a later test in the same session is not surprised.
    assert kv(adc_board.command("adcres 10"))["adc_res_bits"] == 10


def test_adcinput_samples_stay_twelve_bit(adc_board):
    """analogReadResolution() applies to analogRead() and nothing else. A
    capture delivers what the DMA moved out of the data register, so a sketch
    that set 8 bits still gets full-range samples from ADCInput."""
    adc_board.command("adcend")
    adc_board.command("adcres 8")
    assert begin(adc_board, 8000, "01")["adc_begin"] == 1
    adc_board.command("adcdrive 1 1")
    r = capture(adc_board, 300, 2)
    assert r["adc_ch1_min"] > 4000, "capture was scaled to the read resolution"
    adc_board.command("adcend")
    assert kv(adc_board.command("adcres 10"))["adc_res_bits"] == 10


def test_analog_read_refuses_while_a_capture_owns_the_adc(adc_board):
    """There is one ADC. A software conversion started underneath a paced
    capture would return the last channel of the capture's scan -- a real
    reading of a real channel, just not the one asked for -- and would drop a
    scan, putting every later sample on the wrong channel. Both failures are
    silent, which is why this refuses instead."""
    assert begin(adc_board, 8000, "01")["adc_begin"] == 1
    r = kv(adc_board.command("adcvref"))
    assert r["adc_capturing"] == 1
    assert r["avref_raw"] == 0, "analogRead ran against a live capture"

    # And it works again once the capture stops.
    adc_board.command("adcend")
    assert single(adc_board)["avref_raw"] > 300
