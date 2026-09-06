"""The flash translation layer, on its own, before any filesystem uses it.

FAT rewrites its allocation table and directory entries constantly and both
live at fixed sector numbers, so mapped straight onto flash those erase blocks
would take every write in the filesystem while the rest of the partition stays
untouched. The FTL spreads them. It also turns this part's 8 KB erase pages
into the 512-byte LBAs FatFs and USB mass storage expect, which is what lets
the flash volume and the SD card share one FF_MAX_SS.

Tested alone because a bug in here surfaces two layers up as FAT corruption,
where it looks like anything but a mapping bug -- and because the operation
most likely to lose data, garbage collection, is not reachable from the
filesystem API on demand.
"""
import pytest

from conftest import kv


@pytest.fixture(scope="module")
def ftl(fatfs_board):
    """A freshly formatted FTL, once for the module.

    Module-scoped deliberately: the tests below build on each other's writes,
    and reformatting between them would throw away the state that makes
    garbage collection reachable.
    """
    fatfs_board.command("ftlcreate", timeout=5)
    assert kv(fatfs_board.command("ftlformat", timeout=60))["ftl_format"] == 1
    assert kv(fatfs_board.command("ftlstart", timeout=30))["ftl_start"] == 1
    return fatfs_board


def test_geometry_matches_the_partition(ftl):
    """Usable size is the partition less the FTL's fixed reserve.

    Three erase blocks for garbage collection and two for metadata. At 256 KB
    with 8 KB pages that is 32 - 5 = 27 blocks of 16 LBAs = 432. Restated here
    rather than read off the board, because the point is that the two agree.
    """
    info = kv(ftl.command("info", timeout=5))
    ebs = info["fs_size"] // info["eb_bytes"]
    assert info["ftl_ebs"] == ebs, info.raw
    assert info["ftl_lbas"] == (ebs - 5) * (info["eb_bytes"] // 512), info.raw


def test_written_lbas_read_back(ftl):
    """The pattern is a function of the LBA, so a read that returns the wrong
    block fails rather than coincidentally matching."""
    for lba in (0, 1, 17, 100):
        assert kv(ftl.command(f"ftlwrite {lba}", timeout=20))["ftl_write"] == 1
    for lba in (0, 1, 17, 100):
        r = kv(ftl.command(f"ftlverify {lba}", timeout=20))
        assert r["ftl_read"] == 1 and r["ftl_match"] == 1, r.raw


def test_lbas_within_one_erase_block_do_not_alias(ftl):
    """Every slot in an erase block must be independently addressable.

    THE REGRESSION TEST FOR A REAL BUG. The map packs erase block, slot index
    and a valid flag into 16 bits, and upstream hardcodes three bits for the
    index -- exactly right for 4096-byte blocks, which hold 4096/512 = 8
    slots. This part erases 8192 bytes, so a block holds 16 and needs four
    bits; with three, the top bit of every index above 7 was masked away and
    LBA n aliased onto LBA n+8.

    The tests above all passed with that bug present, because none of 0, 1,
    17, 100 is eight apart from another. This writes a contiguous run longer
    than one erase block, which cannot avoid it.
    """
    n = 40  # more than 16 slots, so it spans blocks and exercises every index
    for lba in range(n):
        r = kv(ftl.command(f"ftlwrite {lba}", timeout=20))
        assert r["ftl_write"] == 1, r.raw
    for lba in range(n):
        r = kv(ftl.command(f"ftlverify {lba}", timeout=20))
        assert r["ftl_match"] == 1, (
            f"LBA {lba} read back as something else -- it is sharing a "
            f"physical slot with another LBA")


def test_an_unwritten_lba_reads_as_zeroes(ftl):
    """Not an error, and not stale flash.

    The FTL has no mapping for this LBA and must say so by returning zeroes,
    which is what a filesystem expects from a block it has never written.
    Checked as actually-all-zero rather than merely "not the pattern we would
    have written": returning stale flash would fail the second check and pass
    the first, and would leak a previous filesystem's contents into a freshly
    formatted one.
    """
    r = kv(ftl.command("ftliszero 200", timeout=20))
    assert r["ftl_read"] == 1, r.raw
    assert r["ftl_zero"] == 1, "an unmapped LBA did not read back as zeroes"


def test_rewriting_an_lba_replaces_it(ftl):
    """The log-structured case: the new copy must win, not the old one."""
    assert kv(ftl.command("ftlrewrite 17", timeout=20))["ftl_write"] == 1
    r = kv(ftl.command("ftlreverify 17", timeout=20))
    assert r["ftl_match"] == 1, r.raw
    r = kv(ftl.command("ftlverify 0", timeout=20))
    assert r["ftl_match"] == 1, "rewriting one LBA disturbed another"


def test_survives_garbage_collection(ftl):
    """Write one LBA far more times than the partition has room for.

    The FTL has to reclaim space to keep going, which is the operation that
    moves live data between blocks -- the one most likely to lose it. The
    check afterwards is on an LBA the churn never touched.

    THE ERASE COUNT IS CHECKED, not just the writes. 27 usable erase blocks of
    16 LBAs is 432 slots, so a churn smaller than that fills free space and
    never reclaims anything -- and passes, looking identical to a churn that
    did. The count going up is the only evidence the operation under test
    actually ran.
    """
    before = kv(ftl.command("ftlstats", timeout=20))["ftl_total_pe"]
    assert kv(ftl.command("ftlchurn 2000", timeout=300))["ftl_churn"] == 1
    after = kv(ftl.command("ftlstats", timeout=20))["ftl_total_pe"]
    assert after > before, (
        f"erase count did not move ({before} -> {after}): the churn never "
        f"forced a reclaim, so this test proved nothing")

    r = kv(ftl.command("ftlverify 0", timeout=20))
    assert r["ftl_match"] == 1, "garbage collection lost an untouched LBA"


def test_wear_is_spread_rather_than_concentrated(ftl):
    """The whole point of the layer: after thousands of writes to ONE LBA, no
    single erase block should have taken all of them.

    Without wear levelling a FAT allocation table would sit in one block and
    erase it to death while the rest of the partition stayed pristine.
    """
    s = kv(ftl.command("ftlstats", timeout=20))
    info = kv(ftl.command("info", timeout=5))
    ebs = info["fs_size"] // info["eb_bytes"]
    # If every erase had landed on one block, max would equal the total.
    assert s["ftl_max_pe"] < s["ftl_total_pe"], (
        f"all {s['ftl_total_pe']} erases hit one block: not levelling")
    # And the average block should have seen real use, not just a handful.
    assert s["ftl_total_pe"] >= ebs // 2, s.raw


def test_is_internally_consistent_after_churn(ftl):
    """SPIFTL's own check(): crosslinked LBAs, empty-block accounting, wear
    spread. It knows things about its invariants that a black-box test does
    not, and it is free to ask."""
    assert kv(ftl.command("ftlcheck", timeout=60))["ftl_check"] == 1


def test_the_map_survives_a_reboot(ftl):
    """persist(), reboot, start(): the mapping must come back.

    Without this the filesystem is intact until the first power cycle, which
    is the worst possible time to find out. reboot() rather than a bare reset
    because it waits for the boot banner and prompt, so the command after it
    cannot race the board coming back up.
    """
    assert kv(ftl.command("ftlpersist", timeout=60))["ftl_persist"] == 1
    ftl.reboot(timeout=10)
    assert kv(ftl.command("ftlstart", timeout=60))["ftl_start"] == 1
    r = kv(ftl.command("ftlverify 100", timeout=20))
    assert r["ftl_match"] == 1, r.raw
