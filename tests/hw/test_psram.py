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


def test_the_mapped_pointer_agrees_with_read(psram):
    """Two paths to the same bytes that could silently diverge: one is a CPU
    load through the window, the other an indirect transfer."""
    kv(psram.command("begin", timeout=20))
    kv(psram.command("rw 0x2000", timeout=20))
    r = kv(psram.command("mapcmp 0x2000", timeout=20))
    assert r["base"] == 0x70000000, r.raw
    assert r["mismatch"] == 0, r.raw


def test_a_long_mapped_burst_does_not_disturb_the_rest_of_the_array(psram):
    """The tCEM regression, run in the configuration the library ships.

    The part refreshes itself only while CE# is high and the datasheet caps
    CE#-low at 8 us, but a 64 KB memory-mapped read is milliseconds of traffic.
    This is what lets data() be a plain pointer rather than a guarded accessor,
    so it is kept as a test rather than a note -- it is one chip at one
    temperature, and the whole read path rests on it.

    burst_us is the wall time of the read loop, not a CE#-low time. The
    library does not arm the controller's timeout counter -- the vendor's
    memory-mapped example does not either -- so nothing here is deliberately
    dropping CE# between accesses. That is precisely why the witness rows
    matter.
    """
    kv(psram.command("begin", timeout=20))
    r = kv(psram.command("burst 65536", timeout=40))
    assert r["burst_errors"] == 0, r.raw
    assert r["witness_bad"] == 0, (
        "a long burst corrupted rows elsewhere in the array: %s" % r.raw)
    assert r["burst_us"] > 1000, (
        "the burst was too short to have tested anything: %s" % r.raw)


def test_read_throughput_is_reported(psram):
    """Reported, not asserted. A number that moves with clock settings should
    be visible without failing a build over it."""
    kv(psram.command("begin", timeout=20))
    r = kv(psram.command("burst 65536", timeout=40))
    kbps = 65536 * 1000 // max(r["burst_us"], 1)
    print(f"\n  memory-mapped read: {kbps / 1000.0:.2f} MB/s")
    assert kbps > 0
