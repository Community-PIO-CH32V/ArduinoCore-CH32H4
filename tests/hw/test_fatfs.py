"""FAT on the internal flash: the filesystem, on the translation layer.

Ordered so that a failure names its own layer. test_ftl.py has already proved
the mapping underneath; everything here is FatFs and the FSImpl above it, and
a failure at this point is not a flash problem. That separation is the only
way to tell the two apart without a debugger -- a failed mount looks identical
whether the FTL lost a block or FatFs rejected the geometry.

The board runs the same fatfstest image as test_ftl.py, so these share its
flash. Each test that cares formats first.
"""
import pytest

from conftest import kv


@pytest.fixture(scope="module")
def fs(fatfs_board):
    """A formatted, mounted FAT volume."""
    assert kv(fatfs_board.command("fsformat", timeout=120))["fs_format"] == 1
    r = kv(fatfs_board.command("fsbegin", timeout=60))
    assert r["fs_mount"] == 1, r.raw
    return fatfs_board


def test_mounts_after_format(fs):
    r = kv(fs.command("fsbegin", timeout=60))
    assert r["fs_mount"] == 1, r.raw
    assert r["fs_err"] == "ok", r.raw


def test_reports_the_size_the_ftl_offers(fs):
    """216 KB usable from a 256 KB partition: 32 erase blocks less the FTL's
    fixed five, times 8 KB. FAT's own overhead -- boot sector, allocation
    table, root directory -- comes off that, so the filesystem reports a
    little less than the 432 LBAs the layer below hands it.
    """
    info = kv(fs.command("fsinfo", timeout=20))
    assert info["fs_info"] == 1, info.raw
    assert info["fs_lbas"] == 432, info.raw
    # 432 LBAs is 216 KB; FAT keeps a few for itself.
    assert 190 <= info["fs_total_kb"] <= 216, info.raw


def test_a_file_reads_back(fs):
    assert kv(fs.command("fswrite hello", timeout=60))["fs_rt"] == "ok"
    r = kv(fs.command("fsread hello", timeout=60))
    assert r["fs_rt"] == "ok", r.raw
    assert r["fs_first_ok"] == 1, "the file read back with the wrong contents"
    assert r["fs_rt_size"] > 0, r.raw


def test_a_file_survives_a_reboot(fs):
    """The point of a filesystem. Everything above works equally well against
    a RAM disk until the power goes off."""
    assert kv(fs.command("fswrite persist", timeout=60))["fs_rt"] == "ok"
    fs.reboot(timeout=10)
    r = kv(fs.command("fsbegin", timeout=60))
    assert r["fs_mount"] == 1, r.raw
    r = kv(fs.command("fsread persist", timeout=60))
    assert r["fs_rt"] == "ok", r.raw
    assert r["fs_first_ok"] == 1, r.raw


def test_a_missing_file_does_not_exist(fs):
    """The negative half. exists() returning true for everything would pass
    every test above."""
    assert kv(fs.command("fsexists hello", timeout=20))["fs_exists"] == 1
    assert kv(fs.command("fsexists nosuchfile", timeout=20))["fs_exists"] == 0


def test_the_directory_lists_what_was_written(fs):
    r = kv(fs.command("fslist", timeout=30))
    assert r["fs_entries"] >= 2, r.raw


def test_this_image_is_above_the_minimum(fs):
    """The fixture builds at 256 KB, so this asserts the precondition rather
    than the refusal. The refusal is verified by hand once, by building
    fatfstest at 128 KB -- automating it needs a second image differing only
    in a build flag, which is not worth a flash cycle on every run."""
    info = kv(fs.command("info", timeout=20))
    assert info["fs_size"] >= 256 * 1024, info.raw


def test_autoformat_off_refuses_rather_than_reformatting(fatfs_board):
    """With autoFormat off, a partition holding no FAT volume must fail and
    say why.

    The error string matters more than the false: begin() has four quite
    different failure causes, and a sketch built without -DFS_DEBUG sees no
    message at all. Runs last in the module because it deliberately wipes the
    volume the tests above built.
    """
    fatfs_board.command("fsend", timeout=20)
    # Format the FTL only -- a valid mapping with no FAT volume on it, which
    # is exactly the state a LittleFS partition presents as.
    assert kv(fatfs_board.command("ftlformat", timeout=120))["ftl_format"] == 1
    r = kv(fatfs_board.command("fsnoautoformat", timeout=60))
    assert r["fs_mount"] == 0, r.raw
    assert r["fs_err"] == "no FAT volume, autoformat off", r.raw
