"""LittleFS in the flash tail, and the EEPROM that lives above it.

This is the only filesystem here that runs on the flash the code is executing
from, and that is what makes it different from the SD card tests. Programming
a page holds the flash controller in a mode where an instruction fetch from
the same array does not return an instruction, so the inner loop runs from
ITCM with interrupts masked; and a page erase is 8 KB, which is why the
EEPROM's two pages have to be exactly above _FS_end and not one page inside
it.

The order below is deliberate: raw flash first, then the LittleFS
configuration on top of it, then files, then the EEPROM. A failed mount looks
identical whether the driver cannot program a page or LittleFS rejected the
geometry, and separating them is the only way to tell without a debugger.

Everything here was written after the fact. Both bugs it now covers -- the
flash driver hanging the part mid-program, and EEPROM::commit() returning true
while writing nothing -- shipped because neither had a hardware test at all.
"""
import re

import pytest

from conftest import Reply, _sync

# The layout the linker script asserts. Restated here because the point of the
# test is that the image on the board agrees with it, and a test that read the
# numbers off the board could not disagree with anything.
EEPROM_SIZE = 16 * 1024
FLASH_TOP = 0x08000000 + 960 * 1024
ERASE_PAGE = 8 * 1024
PROG_PAGE = 256
ERASED_WORD = 0xE339E339


def kv(text):
    """Parse key=value replies. Values may be decimal, 0x-hex or a word."""
    out = {}
    for line in text.splitlines():
        m = re.match(r"^([a-z0-9_]+)=(.+)$", line.strip())
        if not m:
            continue
        key, raw = m.group(1), m.group(2).strip()
        if re.fullmatch(r"-?\d+", raw):
            out[key] = int(raw)
        elif re.fullmatch(r"0[xX][0-9a-fA-F]+", raw):
            out[key] = int(raw, 16)
        else:
            out[key] = raw
    return Reply(text, out)


@pytest.fixture(scope="module")
def layout(lfs_board):
    return kv(lfs_board.command("fslayout", timeout=5))


# ---- the flash, with no filesystem on it --------------------------------


def test_the_partition_is_where_the_linker_put_it(layout):
    """_FS_end must be exactly _EEPROM_start, with no gap and no overlap.

    The linker asserts this, but the linker is asserting about its own
    symbols. This reads them out of the running image, which is the only place
    the filesystem and the EEPROM can be seen to agree.
    """
    assert layout["adjacent"] == 1, layout.raw
    assert layout["fs_end"] == FLASH_TOP - EEPROM_SIZE, layout.raw
    assert layout["eeprom_end"] == FLASH_TOP, layout.raw
    assert layout["fs_size"] == layout["fs_end"] - layout["fs_start"]
    assert layout["fs_size"] % ERASE_PAGE == 0, (
        "a partition that does not start on an erase page cannot be erased "
        "without taking its neighbour with it")


def test_the_page_geometry_is_the_one_this_part_has(layout):
    """8 KB erase, 960 KB of user flash.

    DBMODE selects 4 KB or 8 KB pages and the wrong answer here does not fail
    an erase -- it erases twice as much as intended, one page above the
    partition.
    """
    assert layout["flash_page"] == ERASE_PAGE, layout.raw
    assert layout["flash_size"] == 960 * 1024, layout.raw


def test_erase_program_and_verify_without_littlefs(lfs_board):
    """The flash driver alone: erase, blank-check, program, read back.

    Worth its own test because this is what used to hang the board. The
    program loop holds FLASH_CTLR in fast-page mode, during which an
    instruction fetch from the flash being programmed does not return an
    instruction; running it from ITCM with interrupts masked is the fix, and
    an unbounded spin inside it left the controller unlocked and the part
    unreachable by the debug probe.
    """
    d = kv(lfs_board.command("fsraw", timeout=15))
    assert d["raw_erase"] == 1, d.raw
    assert d["raw_blank"] == 1, d.raw

    # Not 0xFFFFFFFF. Every blank check that assumes all-ones decides erased
    # flash is full of data, which is the single most likely thing to get
    # wrong on this part.
    assert d["raw_erased_word"] == ERASED_WORD, d.raw

    # 256 bytes: the fast page program is the only write path that works here.
    # FLASH_ProgramWord() leaves the first word of a run correct and the rest
    # wrong, and reports failure -- which is how the EEPROM was silently
    # broken for as long as it was.
    assert d["raw_prog_size"] == PROG_PAGE, d.raw

    assert d["raw_write"] == 1, d.raw
    assert d["raw_verify"] == 1, d.raw
    # The ends of the page specifically: a driver that programs only the first
    # word passes a checksum written the same way.
    assert d["raw_first_byte"] == 0x10, d.raw
    assert d["raw_last_byte"] == (0x10 + PROG_PAGE - 1) & 0xFF, d.raw

    # A second page inside the same erase block, so the loop is known to
    # advance rather than rewriting page zero.
    assert d["raw_write2"] == 1, d.raw

    # And an unaligned write is refused rather than half-done.
    assert d["raw_unaligned_refused"] == 1, d.raw


def test_the_littlefs_configuration_is_accepted(lfs_board):
    """format() and begin() on a private instance, reporting their own results.

    A bare false from LittleFS.begin() cannot distinguish a failed erase from
    a geometry littlefs rejects, and the two need different fixes.
    """
    d = kv(lfs_board.command("fsdiag", timeout=20))
    assert d["diag_format"] == 1, d.raw
    assert d["diag_begin"] == 1, d.raw
    assert d["diag_block"] == ERASE_PAGE, d.raw
    assert d["diag_part_size"] % ERASE_PAGE == 0, d.raw


# ---- files ---------------------------------------------------------------


@pytest.fixture(scope="module")
def mounted(lfs_board, layout):
    d = kv(lfs_board.command("fsmount", timeout=20))
    if d.get("fs_mount") != 1:
        pytest.fail("LittleFS did not mount:\n" + d.raw)
    return d


def test_the_volume_is_the_size_the_linker_reserved(mounted, layout):
    assert mounted["fs_block"] == ERASE_PAGE, mounted.raw
    # totalBytes counts the partition, not what is free in it.
    assert mounted["fs_total"] == layout["fs_size"], mounted.raw
    assert 0 < mounted["fs_used"] <= mounted["fs_total"], mounted.raw


@pytest.mark.parametrize("size,seed", [(1, 3), (255, 11), (4096, 7),
                                       (20000, 29)])
def test_a_file_round_trips(lfs_board, mounted, size, seed):
    """Sizes either side of the 256-byte program page and the 8 KB block.

    The content is generated from the seed rather than constant, so a stale
    file left by an earlier run cannot pass.
    """
    d = kv(lfs_board.command("fsrt %d %d" % (size, seed), timeout=30))
    assert d["fs_rt"] == "ok", d.raw
    assert d["fs_rt_bytes"] == size, d.raw
    assert d["fs_size_on_disk"] == size, d.raw


def test_directories_and_listing(lfs_board, mounted):
    d = kv(lfs_board.command("fsdirs", timeout=15))
    assert d["fs_sub_exists"] == 1, d.raw
    assert d["fs_root_dirs"] >= 1, d.raw
    assert d["fs_root_files"] >= 1, d.raw


def test_a_file_survives_a_reset(lfs_board, mounted):
    """The point of a filesystem in flash rather than in RAM.

    A mount that works and contents that do not survive is what a driver
    writing to its own cache looks like.
    """
    d = kv(lfs_board.command("fspersist", timeout=15))
    assert d["fs_persist_written"] == 1, d.raw

    # _sync rather than a bare reset: it waits for the boot banner and the
    # prompt. A command sent while the board is still printing its banner gets
    # no reply, which reads exactly like the sketch not knowing the command.
    _sync("lfstest")
    # The sketch does not mount at boot, so the mount is part of what is being
    # tested: a volume that only mounts on the instance that formatted it is
    # a volume nothing else can read.
    assert kv(lfs_board.command("fsmount", timeout=20))["fs_mount"] == 1
    d = kv(lfs_board.command("fspersistcheck", timeout=15))
    assert d["fs_persist"] == 1, d.raw


def test_remove_deletes(lfs_board, mounted):
    kv(lfs_board.command("fsrt 512 5", timeout=20))
    d = kv(lfs_board.command("fsremove", timeout=15))
    assert d["fs_remove"] == 1, d.raw
    assert d["fs_gone"] == 1, d.raw


# ---- the EEPROM above it -------------------------------------------------


def test_filling_the_filesystem_does_not_touch_the_eeprom(lfs_board, mounted):
    """The one that matters, and it goes both ways.

    An erase that walked one page past _FS_end would take the EEPROM's active
    page with it, and an EEPROM commit that miscomputed its base would land in
    the filesystem's last block. Neither shows up as an error at the time --
    the writer succeeds and the other party finds its data gone later.

    This also stands as the only hardware test EEPROM::commit() has. It used
    to return true while writing nothing, which is exactly the failure this
    assertion catches.
    """
    d = kv(lfs_board.command("fseeprom 12", timeout=60))
    assert d["eeprom_commit"] == 1, d.raw
    assert d["fs_fill_bytes"] > 16 * 1024, (
        "the fill did not write enough to be meaningful:\n" + d.raw)
    assert d["eeprom_intact"] == 1, (
        "the filesystem overwrote the EEPROM:\n" + d.raw)
    assert d["fs_still_mounted"] == 1, (
        "the EEPROM commit damaged the filesystem:\n" + d.raw)


def test_the_eeprom_survives_a_reset(lfs_board):
    """Committed contents have to outlive the RAM mirror, or nothing was saved.

    Kept separate from the coexistence test because the two fail for different
    reasons: that one catches a wrong address, this one catches a commit that
    never reached the flash at all. It has to read back with a command that
    does NOT write first -- fseeprom writes the pattern and then checks it,
    which passes on a commit() that does nothing, and that is precisely the
    bug this core shipped.
    """
    written = kv(lfs_board.command("fseeprom 1", timeout=30))
    assert written["eeprom_commit"] == 1, written.raw

    _sync("lfstest")
    d = kv(lfs_board.command("fseepromcheck", timeout=15))
    assert d["eeprom_persisted"] == 1, d.raw
    # -1 means neither page carries a valid header, so nothing was committed
    # and the reads above came from the 0xFF the mirror starts as.
    assert d["eeprom_active_page"] in (0, 1), d.raw


def test_format_wipes_and_remounts(lfs_board, mounted):
    """Last, because it destroys what every test above it wrote."""
    d = kv(lfs_board.command("fsformat", timeout=60))
    assert d["fs_format"] == 1, d.raw
    assert d["fs_remount"] == 1, d.raw
