"""Flash and SD, mounted at the same time.

THIS IS WHAT THE RESTRUCTURE WAS FOR, and it is the only file here that would
notice if it had gone wrong. test_fatfs.py mounts the flash volume,
test_filesystem.py mounts the SD one, and a single-volume FatFs quietly
serving whichever mounted first passes both of them -- one volume is exactly
what FatFs used to be built for here.

Needs a card on the SDMMC default mapping (CK PC12, CMD PD2, D0 PC8). Without
one these skip: an unwired bench is a missing precondition, not a failure.
"""
import pytest

from conftest import kv


@pytest.fixture(scope="module")
def both(two_volume_board):
    assert kv(two_volume_board.command("format", timeout=60))["flash_format"] == 1
    r = kv(two_volume_board.command("both", timeout=60))
    if r.get("sd_present") == 0:
        pytest.skip("no SD card wired to the SDMMC pins")
    assert r["flash_mount"] == 1, r.raw
    assert r["sd_mount"] == 1, r.raw
    return two_volume_board, r


def test_both_mount_together(both):
    _, r = both
    assert r["flash_err"] == "ok", r.raw


def test_the_two_volumes_are_different_devices(both):
    """A card is orders of magnitude larger than the flash partition.

    If one volume were answering for both mounts the two sizes would agree,
    and they would agree whichever way the bug went.
    """
    _, r = both
    assert r["flash_kb"] < 1024, r.raw          # 198 KB of internal flash
    assert r["sd_kb"] > 100 * 1024, r.raw       # a real card, megabytes at least
    assert r["sd_kb"] != r["flash_kb"], r.raw


def test_each_volume_keeps_its_own_file(both):
    board, _ = both
    r = kv(board.command("write", timeout=60))
    assert r["flash_write"] == 1 and r["sd_write"] == 1, r.raw
    r = kv(board.command("read", timeout=60))
    assert r["flash_match"] == 1, "the flash volume read back the wrong contents"
    assert r["sd_match"] == 1, "the SD volume read back the wrong contents"


def test_neither_volume_can_see_the_other_s_file(both):
    """The sharpest form of the question.

    Each volume has a file named after itself. If one FatFs volume were
    serving both mounts, each would find the other's.
    """
    board, _ = both
    r = kv(board.command("crosscheck", timeout=30))
    assert r["flash_has_sd_file"] == 0, "the flash volume can see the SD card's file"
    assert r["sd_has_flash_file"] == 0, "the SD card can see the flash volume's file"


def test_interleaved_writes_do_not_cross_volumes(both):
    """Alternate between the volumes rather than finishing one first.

    A shared work area, a shared drive number, or a static that should have
    been per-volume all survive writing one volume and then the other. None of
    them survives alternating.
    """
    board, _ = both
    r = kv(board.command("interleave 16", timeout=90))
    assert r["interleave_write"] == 1, r.raw
    assert r["flash_match"] == 1, r.raw
    assert r["sd_match"] == 1, r.raw
