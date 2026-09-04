"""I2C slave mode, against a master on the same chip and one bus.

Wire (I2C1, PB6/PB7) is the master and Wire1 (I2C4, PD12/PD13) the slave,
joined by two jumpers -- PB6 to PD12 and PB7 to PD13. I2C is multi-drop, so
they share the bus the SSD1306 is already on and use its pull-ups; this part
has none of its own in open-drain mode. The slave answers at 0x42, which is
not the display's 0x3C.

Every test skips when the wiring is absent.
"""
import re

import pytest

from conftest import Reply

SLAVE_ADDR = 0x42


def kv(text):
    out = {}
    for line in text.splitlines():
        m = re.match(r"^([a-z0-9_]+)=(0x[0-9a-fA-F]+|-?\d+)$", line.strip())
        if m:
            v = m.group(2)
            out[m.group(1)] = int(v, 16) if v.startswith("0x") else int(v)
    return Reply(text, out)


@pytest.fixture(scope="module")
def wired(wire_slave_board):
    """The two jumpers, or a skip.

    Probed by asking the master to address the slave, which is the minimum
    that has to work: if the address is not acknowledged, nothing else can be.
    """
    out = wire_slave_board.command("wisscan", timeout=20)
    if ("wis_found=0x%X" % SLAVE_ADDR).lower() not in out.lower():
        pytest.skip(
            "no I2C master/slave wiring: needs PB6-PD12 (SCL) and "
            "PB7-PD13 (SDA)")
    return wire_slave_board


def test_the_pins_resolve_to_two_different_peripherals(wire_slave_board):
    """Needs no wiring -- it asks the pin map, not the bench."""
    d = kv(wire_slave_board.command("wisinfo", timeout=5))
    assert d["wis_master"] == 1, d.raw
    assert d["wis_slave"] == 4, d.raw
    assert d["wis_addr"] == SLAVE_ADDR, d.raw


def test_the_slave_answers_a_bus_scan(wired):
    """The one thing that has to work before anything else can.

    The display must still be there too: a slave that holds SDA low between
    transfers takes the whole bus with it, and a scan that finds the slave and
    nothing else is what that looks like.
    """
    d = kv(wired.command("wisscan", timeout=20))
    assert d["wis_scan_count"] >= 2, (
        "expected at least the slave at 0x42 and the display at 0x3C:\n%s"
        % d.raw)


@pytest.mark.parametrize("n", [1, 2, 8, 32, 128])
def test_a_master_write_reaches_the_receive_handler(wired, n):
    """One byte is the interesting size.

    A single-byte write gives the slave one interrupt to notice the address,
    take the byte and see the STOP, and an implementation that waits for BTF
    rather than RXNE loses exactly that case while every longer write works.
    """
    d = kv(wired.command("wiswrite %d" % n, timeout=15))
    assert d["wis_write_rc"] == 0, (
        "the master could not address the slave:\n%s" % d.raw)
    assert d["wis_write_cb"] == 1, (
        "the receive handler did not fire exactly once:\n%s" % d.raw)
    assert d["wis_write_len"] == n, d.raw
    assert d["wis_write_match"] == 1, d.raw


@pytest.mark.parametrize("n", [1, 2, 8, 32, 128])
def test_a_master_read_is_served_by_the_request_handler(wired, n):
    """The last byte is the one that matters.

    A master ends a read by NOT acknowledging the final byte, which raises the
    slave's acknowledge-failure flag -- an error flag for the normal end of
    every read. A slave that treats it as an error, or leaves it set, answers
    the first read and then goes silent, so this reads repeatedly elsewhere to
    catch that.
    """
    d = kv(wired.command("wisread %d" % n, timeout=15))
    assert d["wis_read_got"] == n, d.raw
    assert d["wis_read_match"] == 1, (
        "the master did not get what the request handler wrote:\n%s" % d.raw)


def test_repeated_transfers_keep_working(wired):
    """Three rounds of write-then-read.

    This is the test for flags left set. A slave that does not clear its
    acknowledge-failure or stop flag serves the first transfer perfectly and
    then never matches its address again, which every single-shot test above
    reports as a pass.
    """
    d = kv(wired.command("wisrepeat", timeout=20))
    assert d["wis_repeat_ok"] == 6, (
        "expected 3 writes and 3 reads to all work, got %d of 6:\n%s"
        % (d["wis_repeat_ok"], d.raw))


def test_a_read_with_nothing_queued_does_not_repeat_the_last_reply(wired):
    """Stale data is the failure that looks like success."""
    d = kv(wired.command("wisempty", timeout=15))
    assert d["wis_empty_stale"] == 0, (
        "a read the handler supplied nothing for returned the previous "
        "reply:\n%s" % d.raw)


def test_the_display_still_works_with_a_slave_on_the_bus(wired):
    """Sharing the bus is the whole point of the wiring, so prove it shares.

    The slave and the display are different devices at different addresses on
    the same two wires; if adding the slave broke the display, the wiring
    would be unusable however well the slave itself behaved.
    """
    d = kv(wired.command("wisdisplay", timeout=10))
    assert d["wis_display_rc"] == 0, (
        "the SSD1306 stopped acknowledging once the slave joined the "
        "bus:\n%s" % d.raw)
