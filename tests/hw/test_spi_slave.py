"""SPI slave mode, against a master on the same chip.

SPI1 drives SPI4 over four jumpers -- PA5/PE2, PA7/PE6, PA6/PE5 and PE3/PE4 --
and the PA6-PA7 loopback jumper must be OUT, since it would short the master's
MOSI onto its own MISO and fight the slave. Every test here skips when the
wiring is absent, because a missing wire is a missing precondition and not a
broken driver.

ONE OF THESE TESTS IS LOAD-BEARING FOR SOMETHING ELSE. There is no published
alternate-function table for this part's SPI NSS, so the entry that says PE4
is SPI4's chip select is an inference -- see g_spi_nss_map in the variant.
test_chip_select_actually_selects is what settles it: it clocks a frame with
CS held high, and a slave that answers anyway is a slave whose NSS is not on
that pin. Every other test here passes either way, which is exactly why that
one has to exist.
"""
import re

import pytest

from conftest import Reply


def kv(text):
    out = {}
    for line in text.splitlines():
        m = re.match(r"^([a-z0-9_]+)=(0x[0-9a-fA-F]+|-?\d+)$", line.strip())
        if m:
            v = m.group(2)
            out[m.group(1)] = int(v, 16) if v.startswith("0x") else int(v)
    return Reply(text, out)


@pytest.fixture(scope="module")
def wired(spi_slave_board):
    """The four jumpers, or a skip.

    Probed with a real transfer rather than by driving pins: the slave has to
    be listening for the test to mean anything, and a GPIO-level check would
    pass on wiring that the peripherals cannot actually use.
    """
    d = kv(spi_slave_board.command("spisxfer 4", timeout=10))
    if d.get("spis_frame") != 1 or d.get("spis_slave_got") != 1:
        pytest.skip(
            "no SPI master/slave wiring: needs PA5-PE2, PA7-PE6, PA6-PE5, "
            "PE3-PE4, and the PA6-PA7 loopback jumper removed")
    return spi_slave_board


def test_the_pins_resolve_to_two_different_peripherals(spi_slave_board):
    """Needs no wiring at all -- it is asking the pin map, not the bench.

    A master and a slave that resolved to the SAME peripheral would be one
    peripheral talking to itself, which is not a thing, and every transfer
    test would then fail for a reason that has nothing to do with SPI.
    """
    d = kv(spi_slave_board.command("spisinfo", timeout=5))
    assert d["spis_master"] == 1, d.raw
    assert d["spis_slave"] == 4, d.raw
    assert d["spis_master"] != d["spis_slave"], d.raw


@pytest.mark.parametrize("n", [1, 2, 8, 64, 256])
def test_a_frame_goes_both_ways(wired, n):
    """Full duplex, so both directions are checked separately.

    They fail differently: the master reading what the slave queued proves
    MISO and the slave's transmit path, the slave reporting what the master
    sent proves MOSI and its receive path. A test that checked only one would
    pass with half the wiring in place.

    256 is the buffer length, which is where an off-by-one in the queue shows
    up; 1 and 2 are where the interrupt has the least time to react.
    """
    d = kv(wired.command("spisxfer %d" % n, timeout=15))
    assert d["spis_frame"] == 1, "the slave never reported a frame:\n%s" % d.raw
    assert d["spis_rxlen"] == n, d.raw
    assert d["spis_slave_got"] == 1, (
        "the slave did not receive what the master sent -- MOSI:\n%s" % d.raw)
    assert d["spis_master_got"] == 1, (
        "the master did not receive what the slave queued -- MISO:\n%s" % d.raw)


def test_the_sent_callback_fires_once_per_frame(wired):
    """Once, not zero and not per byte.

    Zero means the queue never drained, which would also mean the master read
    padding; per byte means it is reporting the wrong event entirely.
    """
    d = kv(wired.command("spisxfer 16", timeout=10))
    assert d["spis_sent_cb"] == 1, d.raw


@pytest.mark.parametrize("mode", [0, 1, 2, 3])
def test_every_clock_mode_works(wired, mode):
    """Both ends are configured from the same number, so this checks that the
    slave's CPOL/CPHA decoding matches the master's rather than that either is
    right in the abstract. A slave that ignored the mode would pass mode 0 and
    fail the rest, which is the failure this is shaped to catch.
    """
    d = kv(wired.command("spisxfer 8 1000000 %d" % mode, timeout=10))
    assert d["spis_frame"] == 1, d.raw
    assert d["spis_slave_got"] == 1, d.raw
    assert d["spis_master_got"] == 1, d.raw


@pytest.mark.parametrize("hz", [400000, 1000000, 4000000])
def test_the_slave_keeps_up(wired, hz):
    """How fast a byte-at-a-time interrupt slave can be clocked.

    This is a real limit, not a formality: the slave services one interrupt
    per byte, so at some clock the next byte arrives before the last was read
    and the frame comes back short or shifted. Recording where that is means
    a future change that makes the interrupt slower gets caught here rather
    than in somebody's application.
    """
    d = kv(wired.command("spisxfer 32 %d 0" % hz, timeout=10))
    assert d["spis_frame"] == 1, d.raw
    assert d["spis_slave_got"] == 1, (
        "the slave fell behind at %d Hz:\n%s" % (hz, d.raw))
    assert d["spis_master_got"] == 1, d.raw


def test_a_frame_with_nothing_queued_does_not_repeat_the_last_one(wired):
    """Stale data is the failure that looks like success.

    A slave that re-sends the previous frame's buffer works perfectly in any
    test that queues data every time -- which is every other test here.
    """
    d = kv(wired.command("spisstale", timeout=10))
    assert d["spis_stale_frame"] == 1, d.raw
    assert d["spis_stale_repeat"] == 0, (
        "the second frame returned the first frame's data:\n%s" % d.raw)
    assert d["spis_stale_slave_got"] == 1, (
        "the slave stopped receiving after a frame it had nothing to send "
        "for:\n%s" % d.raw)


def test_chip_select_actually_selects(wired):
    """The test that settles what PE4 is.

    A frame clocked with CS held HIGH must produce nothing: no frame reported,
    and the master reading back something other than what the slave queued.
    If the slave answers anyway then PE4 is not SPI4's NSS, the peripheral is
    running permanently selected, and the row in g_spi_nss_map is wrong and
    should be deleted -- SPISlave falls back to software chip select and keeps
    working, which is why a missing row costs less than a wrong one.
    """
    info = kv(wired.command("spisinfo", timeout=5))
    if info.get("spis_hardcs") != 1:
        pytest.skip("no hardware chip select configured; nothing to check")

    d = kv(wired.command("spisdeselect", timeout=10))
    assert d["spis_desel_frame"] == 0, (
        "the slave reported a frame while it was NOT selected -- PE4 is "
        "probably not SPI4's NSS; see g_spi_nss_map in the variant:\n%s"
        % d.raw)
    assert d["spis_desel_answered"] == 0, (
        "the slave drove MISO while it was NOT selected:\n%s" % d.raw)
