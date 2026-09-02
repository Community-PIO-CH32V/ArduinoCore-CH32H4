"""The SD card block layer, on the SDMMC controller.

No filesystem here on purpose. Nearly every way this can fail fails in the
block driver -- identification, the inter-command gap, the arming asymmetry
between reads and writes, the bounce buffers -- and a FatFS layer on top would
collapse all of them into one "mount failed".

Needs a card on the SDMMC default mapping: CK PC12, CMD PD2, D0 PC8.
"""
import pytest


from conftest import Reply


def _kv(out):
    d = {}
    for line in out.splitlines():
        for part in line.split():
            if "=" in part:
                k, _, v = part.partition("=")
                d[k.strip()] = v.strip()
    return Reply(out, d)


@pytest.fixture(scope="module")
def sd(sd_board):
    """A card, identified. Skips if there is nothing wired up."""
    d = _kv(sd_board.command("sdinit 1 20000000", timeout=20.0))
    if d.get("sd_init") != "ok":
        pytest.skip(f"no card responded (sd_init={d.get('sd_init')}). "
                    "Wired to CK PC12, CMD PD2, D0 PC8?")
    return {"b": sd_board, "d": d}


def test_the_io_rail_is_high_enough(sd_board):
    """Every SDMMC pin is on VIO18.

    At the 1.2 V the chip powers up with, a card sees no valid high level at
    all and CMD0 gets no response -- a failure that looks exactly like bad
    wiring. PWR_CTLR bits 12:10 select the rail; 3 is 3.3 V.
    """
    d = _kv(sd_board.command("vio18", timeout=5.0))
    assert int(d["vio18_sel"]) >= 2, ("VIO18 is below 2.5 V, so no card can "
                                      "answer regardless of the driver", d)


def test_the_card_identifies(sd):
    """CMD0 through CMD9, and a capacity that came out of the CSD."""
    d = sd["d"]
    assert d["sd_type"] in ("SDHC", "SDSC"), d
    assert d["sd_rca"] != "0x0", ("the card kept address zero, which means "
                                  "CMD3 did not really take", d)
    blocks = int(d["sd_blocks"])
    assert blocks > 0, d
    # Nothing smaller than 8 MB exists, and the CSD decode is the part most
    # likely to be wrong in a way that still produces a plausible number.
    assert blocks > 16384, ("suspiciously small capacity -- check the CSD "
                            "decode", d)


def test_a_single_block_reads(sd):
    d = _kv(sd["b"].command("sdread 0 1", timeout=10.0))
    assert d["sd_read"] == "ok", d
    assert len(d["sd_read_head"]) == 32, d


def test_reading_the_same_block_twice_agrees(sd):
    """The DMA_BEG1 rewind trap.

    Writing that register with the value it already holds starts a transfer
    without moving the pointer back to the start of the buffer, so a second
    read from the same address returns whatever followed the first one. The
    driver alternates between two bounce buffers so the register always
    changes; this is what proves it.
    """
    d = _kv(sd["b"].command("sdrepeat 0", timeout=10.0))
    assert d["sd_repeat"] == "ok", ("three reads of one block disagreed", d)


def test_multi_block_reads(sd):
    d = _kv(sd["b"].command("sdread 0 8", timeout=10.0))
    assert d["sd_read"] == "ok", d


def test_write_read_verify_and_restore(sd):
    """A block, written and read back byte-exact, then put back as found.

    The pattern differs in every block and every position, so a transfer that
    repeats a block, drops one, or is off by one all show up as a mismatch
    rather than as a plausible result.
    """
    d = _kv(sd["b"].command("sdwv 1000 1", timeout=15.0))
    assert d["sd_wv"] == "ok", d
    assert d["sd_wv_match"] == "1", d
    assert d["sd_wv_restored"] == "1", d


def test_multi_block_write_read_verify(sd):
    """Eight blocks at once, which is the whole bounce buffer.

    A multi-block write is not a loop of single-block writes in this
    controller: every block after the first has to be kicked off by writing
    WRITE_CONT, and arming the DMA before the command -- which is what a read
    needs -- makes the command itself time out.
    """
    d = _kv(sd["b"].command("sdwv 2000 8", timeout=20.0))
    assert d["sd_wv"] == "ok", d


def test_a_run_longer_than_the_bounce_buffer(sd):
    """64 blocks, so the chunking loop runs eight times and the two bounce
    buffers alternate throughout. Verified byte by byte, not by checksum."""
    d = _kv(sd["b"].command("sdbulk 100000 64", timeout=40.0))
    assert d["sd_bulk"] == "ok", d
    assert int(d["sd_bulk_blocks"]) == 64, d
    # Not a performance gate -- a floor low enough that only a broken transfer
    # path trips it, so a slow card never flakes this.
    assert int(d["sd_bulk_read_kbs"]) > 200, d


def test_reinit_after_a_write_survives(sd):
    """begin() straight after a bulk write, repeatedly.

    A card holds DAT0 low while it programs, and the SD specification requires
    the clock to keep running until it lets go. Tearing the controller down
    without waiting wedges the card: it then ignores CMD0 too, so the next
    begin() times out in ACMD41 and every one after that does as well, until
    the board is power-cycled.

    That is not hypothetical -- this loop failed on its third round before
    ch32h4_sd_end() learned to wait. Five rounds, because it was intermittent.
    """
    for i in range(5):
        b = _kv(sd["b"].command("sdbulk 200000 64", timeout=40.0))
        assert b["sd_bulk"] == "ok", (f"bulk failed on round {i}", b)
        r = _kv(sd["b"].command("sdinit 1 20000000", timeout=20.0))
        assert r["sd_init"] == "ok", (f"re-init failed on round {i}", r)


def test_high_speed_mode(sd):
    """CMD6 into high speed, then 50 MHz.

    A card that will not switch has to be held at 25 MHz rather than clocked
    past its rating and hoped for, so both outcomes are acceptable -- what is
    not acceptable is claiming high speed and then failing to transfer.
    """
    d = _kv(sd["b"].command("sdinit 1 50000000", timeout=20.0))
    assert d["sd_init"] == "ok", d
    if d["sd_high_speed"] == "1":
        assert int(d["sd_freq"]) > 25000000, ("high speed was negotiated but "
                                              "the clock did not follow", d)
    else:
        assert int(d["sd_freq"]) <= 25000000, ("the card declined high speed, "
                                               "so it must stay at 25 MHz", d)
    b = _kv(sd["b"].command("sdbulk 100000 64", timeout=40.0))
    assert b["sd_bulk"] == "ok", ("transfers failed at the negotiated clock", b)

    # Leave the card at the speed the rest of the module expects.
    sd["b"].command("sdinit 1 20000000", timeout=20.0)
