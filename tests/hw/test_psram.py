"""8 MB of QSPI pseudo-SRAM on PE10-PE15.

Needs the ESP-PSRAM64H breakout wired. Without it begin() fails and every
test here skips: an unwired bench is a missing precondition, not a failure.

The identification is exact rather than approximate. MFID 0x0D and KGD 0x5D
are fixed constants in the die, so they cannot be produced by a floating bus
-- which is the failure a "did anything come back" check would pass.
"""
import pytest

from conftest import kv


@pytest.fixture(scope="module")
def psram(psram_board):
    r = kv(psram_board.command("begin", timeout=20))
    if r["begun"] == 0:
        pytest.skip("no PSRAM responding on PE10-PE15")
    return psram_board


def test_the_chip_identifies_itself(psram):
    r = kv(psram.command("begin", timeout=20))
    assert r["mfid"] == "D", r.raw
    assert r["detected"] == 1, r.raw
    assert r["size"] == 8 * 1024 * 1024, r.raw


def test_the_clock_divides_hclk_not_the_core_clock(psram):
    """25 MHz off a 100 MHz HCLK. Deriving the prescaler from
    SystemCoreClock, which is 400 MHz on the V5F, would give a divider four
    times too small and a clock four times too fast."""
    r = kv(psram.command("begin", timeout=20))
    assert 20000000 <= r["clock"] <= 25000000, r.raw


def test_the_eid_is_stable_across_reboots(psram):
    """It is a per-die serial. A changing one means the link is marginal and
    the ID read is picking up noise, which would otherwise look like success."""
    first = kv(psram.command("begin", timeout=20))["eid"]
    psram.reboot(timeout=10)
    second = kv(psram.command("begin", timeout=20))["eid"]
    assert first == second, f"EID changed: {first} -> {second}"


def test_begin_refuses_an_unreachable_clock(psram):
    assert kv(psram.command("badclock", timeout=20))["begun"] == 0


def test_a_second_begin_without_end_is_refused(psram):
    kv(psram.command("begin", timeout=20))
    assert kv(psram.command("again", timeout=20))["begun"] == 0


@pytest.mark.parametrize("addr", ["0", "0x1000", "0x400000", "0x7FFFC0"])
def test_a_pattern_reads_back(psram, addr):
    kv(psram.command("begin", timeout=20))
    r = kv(psram.command(f"rw {addr}", timeout=20))
    assert r["wrote"] == 64, r.raw
    assert r["read"] == 64, r.raw
    assert r["match"] == 1, f"read-back differs at {addr}: {r.raw}"


def test_transfers_clamp_at_the_end_of_the_device(psram):
    kv(psram.command("begin", timeout=20))
    r = kv(psram.command("clamp", timeout=20))
    assert r["tail"] == 16, "a read overrunning the end should come back short"
    assert r["past"] == 0, "a read starting past the end should transfer nothing"


def test_every_address_line_decodes(psram):
    """A unique byte at each power-of-two boundary.

    This is what actually establishes 8 MB. The cruder "top differs from
    bottom" check passes on a part half the size, because a missing top
    address line folds 0x7FFFF8 onto 0x3FFFF8 rather than onto zero.
    """
    kv(psram.command("begin", timeout=20))
    r = kv(psram.command("density", timeout=30))
    assert r["boundaries_bad"] == 0, r.raw
    assert r["zero_ok"] == 1, "address 0 was disturbed by a boundary write"
