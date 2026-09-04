"""SPI, including the DMA block path.

The board carries a PA6-PA7 jumper -- MISO tied to MOSI -- so a transfer
returns what it sent. That is what makes these tests about a wire rather than
about a register: a driver can configure the peripheral perfectly, report every
flag correctly, and move nothing.

The DMA path is the reason this file grew. Block transfers used to be a
byte-at-a-time polled loop that plateaued near 2.3 Mbit/s no matter the clock,
so the bus idled nine tenths of the time at 24 MHz. They now go through two DMA
channels and run at essentially the full bus rate.
"""
import re

import pytest

from conftest import Reply


def kv(text):
    out = {}
    for line in text.splitlines():
        m = re.match(r"^([a-z0-9_]+)=(0x[0-9a-fA-F]+|-?\d+|\w+)$", line.strip())
        if m:
            v = m.group(2)
            out[m.group(1)] = int(v, 16) if v.startswith("0x") else (
                int(v) if re.fullmatch(r"-?\d+", v) else v)
    return Reply(text, out)


@pytest.fixture(scope="module")
def looped(spi_board):
    """SPI with the loopback jumper confirmed, or a skip."""
    d = kv(spi_board.command("spiloop", timeout=5))
    if d.get("spi_loopback") != "ok":
        pytest.skip("no PA6-PA7 loopback jumper")
    return spi_board


def test_the_peripheral_comes_from_the_pins(spi_board):
    """The default pin trio must resolve to a real peripheral.

    begin() derives the SPI id from the pins rather than being told, so a pin
    set that no single peripheral can serve is the failure to catch.
    """
    d = kv(spi_board.command("spiinfo", timeout=5))
    assert d["spi_peripheral"] in (1, 2, 3, 4), d.raw


def test_pins_no_peripheral_can_serve_are_reported(spi_board):
    d = kv(spi_board.command("spibadpins", timeout=5))
    assert d["bad_peripheral"] == 0, (
        "a trio spanning two peripherals must report 0, not half-configure "
        "one of them:\n" + d.raw)


@pytest.mark.parametrize("n", [
    8,      # polled: below the DMA threshold
    31,     # polled: the last size before it
    32,     # the first size that uses DMA
    512,    # comfortably DMA
    1024,
])
def test_a_block_round_trips(looped, n):
    """Both buffer forms, on both sides of the threshold.

    In place is the one worth having: the DMA transmit channel reads each byte
    before the receive channel overwrites it, staying two byte-times ahead. If
    that were wrong the buffer would come back carrying its own tail, and only
    at DMA sizes.
    """
    d = kv(looped.command("spiblock %d" % n, timeout=15))
    assert d["spi_inplace"] == 1, d.raw
    assert d["spi_split"] == 1, d.raw


def test_half_duplex_transfers(looped):
    """Null on either side.

    A send-only DMA transfer that returns before the bus goes idle leaves a
    byte still going out, and the next endTransaction() raises chip select
    underneath it -- which corrupts the last byte of every write on a real
    device while every test that reads something back still passes.
    """
    d = kv(looped.command("spihalfduplex", timeout=15))
    assert d["spi_rxonly_ff"] == 1, d.raw
    # 256 bytes at the 6.25 MHz the divider actually produces is 327 us.
    assert 250 < d["spi_txonly_us"] < 3000, (
        "send-only transfer took %d us; expected around 330" % d["spi_txonly_us"])


def test_block_transfers_use_most_of_the_bus(looped):
    """The number that says DMA is really carrying it.

    Requesting 24 MHz gives 12.5 MHz, because the divider is a power of two
    below HCLK. The polled loop managed 2269 kbit/s of that -- 18% -- and the
    DMA path should be near the whole of it. The bound is deliberately loose:
    this is asking whether the CPU is still in the way, not measuring jitter.
    """
    d = kv(looped.command("spibench 24000000", timeout=20))
    assert d["spi_bench_kbits"] > 8000, (
        "%d kbit/s at a 12.5 MHz bus -- the polled path managed 2269, so "
        "anything this low means DMA is not carrying the transfer:\n%s"
        % (d["spi_bench_kbits"], d.raw))
