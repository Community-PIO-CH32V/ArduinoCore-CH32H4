"""I2C against the SSD1306 on the board.

Two things are worth guarding here, and throughput is only one of them.

WHY THERE IS NO DMA PATH. Measured on this board, a 128-byte write reaches
92% of the theoretical bus time at 400 kHz and 97% at 100 kHz. I2C is thirty
to sixty times slower on the wire than SPI, so the per-byte polling that cost
SPI five sixths of its bus costs I2C almost nothing. Adding DMA here would
recover single-digit percentages and add a second set of failure modes. These
tests record the measurement so that a future change which does make Wire slow
is caught -- the reason for not optimising is a number, and numbers rot.

SHORT READS. A one-byte read has to clear the ACK bit before it clears the
address phase; the peripheral decides what to drive on the ninth clock as soon
as ADDR is released. Get it wrong and the device sends a byte nobody asked
for, which desynchronises every transfer after it -- so the symptom shows up
somewhere else and looks like a different bug entirely.
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
def display(wire_board):
    """A device on the bus, or a skip."""
    out = wire_board.command("i2cscan", timeout=10)
    if "i2c_found=" not in out:
        pytest.skip("nothing answers on I2C; is the SSD1306 attached?")
    return wire_board


def test_the_peripheral_comes_from_the_pins(wire_board):
    d = kv(wire_board.command("i2cinfo", timeout=5))
    assert d["i2c_peripheral"] in (1, 2, 3, 4), d.raw


@pytest.mark.parametrize("n", [1, 2, 3, 8])
def test_a_short_read_returns_exactly_what_was_asked_for(display, n):
    """One and two bytes are the cases with special ACK handling.

    Returning fewer means the read gave up; returning more is impossible
    through this API but shows up as the following write failing, which the
    second assertion catches.
    """
    d = kv(display.command("i2cread %d" % n, timeout=10))
    assert d["i2c_read_got"] == n, d.raw
    assert d["i2c_read_avail"] == n, d.raw
    assert d["i2c_after_read_rc"] == 0, (
        "the bus was left unusable after a %d-byte read -- a stuck ACK looks "
        "exactly like this:\n%s" % (n, d.raw))


def test_an_absent_device_nacks_rather_than_hanging(display):
    """Address NACK is 2, not 4. A bus scan depends on telling them apart."""
    d = kv(display.command("i2cabsent", timeout=10))
    assert d["i2c_absent_rc"] == 2, d.raw
    assert d["i2c_absent_read"] == 0, d.raw


@pytest.mark.parametrize("hz,floor", [(100000, 90), (400000, 85)])
def test_writes_run_near_the_bus_limit(display, hz, floor):
    """The measurement that says a DMA path is not needed.

    Measured 97% at 100 kHz and 92% at 400 kHz; the floors are set below that
    with room for jitter, because this is asking whether the driver is still
    bus-limited, not measuring it precisely.
    """
    d = kv(display.command("i2cbench %d" % hz, timeout=15))
    assert d["i2c_bench_rc"] == 0, d.raw
    assert d["i2c_bench_pct"] >= floor, (
        "%d%% of the theoretical bus time at %d Hz, expected at least %d%%. "
        "Wire has no DMA path because it did not need one; this says it now "
        "does.\n%s" % (d["i2c_bench_pct"], hz, floor, d.raw))
