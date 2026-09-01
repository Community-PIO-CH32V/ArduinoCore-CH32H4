"""FatFs behind the FS/File/Dir API, and the classic SD shim over it.

Both APIs are exercised, because they are separate code paths onto one
filesystem and a shim that compiles is not a shim that works. FILE_WRITE in
particular means *append* in the classic library, and a shim that maps it to
"w" silently eats every log a sketch has ever written.

Needs a card on the SDMMC default mapping: CK PC12, CMD PD2, D0 PC8.
"""
import time

import pytest


def _kv(out):
    d = {}
    for line in out.splitlines():
        for part in line.strip().split():
            if "=" in part:
                k, _, v = part.partition("=")
                d[k.strip()] = v.strip()
    return d


@pytest.fixture(scope="module")
def fsb(fs_board):
    """A mounted filesystem, or a skip."""
    d = _kv(fs_board.command("fsmount 1 20000000", timeout=30.0))
    if d.get("fs_mount") != "1":
        pytest.skip("no mountable filesystem (fs_mount="
                    f"{d.get('fs_mount')}). Card present and FAT-formatted?")
    return {"b": fs_board, "d": d}


def test_the_volume_mounts(fsb):
    d = fsb["d"]
    assert int(d["fs_total_kb"]) > 0, d
    assert int(d["fs_cluster"]) >= 512, d
    # The volume cannot be bigger than the card it is on. A capacity read out
    # of the wrong CSD field, or a 32-bit overflow on a large card, shows up
    # here and essentially nowhere else.
    assert int(d["fs_total_kb"]) <= int(d["card_kb"]), d


def test_the_controller_can_be_restarted(fsb):
    """begin() twice, which is not the same as begin() once.

    The reset de-assert in sd_controller_reset() is a read-modify-write with no
    read-back, and a dropped one leaves the block held in reset. The first
    bring-up works because the block was never in reset; the second does not,
    and every one after it fails until the board is reset. The tell is a
    timeout with an EMPTY command trace -- the command never reached the bus.
    """
    try:
        for i in range(4):
            d = _kv(fsb["b"].command("sdraw", timeout=20.0))
            assert d["sd_begin"] == "0", (f"begin failed on cycle {i}", d)
            fsb["b"].command("sdrawend", timeout=10.0)
    finally:
        # These cycles tore the card down under the mounted filesystem, and
        # SDFS still believes it is mounted. Remount, or every test after this
        # one fails on an open that has no card beneath it.
        remount = _kv(fsb["b"].command("fsmount 1 20000000", timeout=30.0))
        assert remount.get("fs_mount") == "1", ("could not remount", remount)


def test_a_file_round_trips(fsb):
    """4 KB, generated rather than constant, checked byte by byte.

    Constant content would pass with a short read, a stale buffer or an
    off-by-one; content derived from the offset does not.
    """
    d = _kv(fsb["b"].command("fsrt 4096", timeout=30.0))
    assert d["fs_rt"] == "ok", d
    assert int(d["fs_rt_size"]) == 4096, d


def test_a_file_larger_than_the_bounce_buffer_round_trips(fsb):
    """256 KB: many clusters, many multi-block transfers, one FAT chain walk.

    A single small file fits in one cluster and never exercises the allocation
    path at all.
    """
    d = _kv(fsb["b"].command("fsrt 262144", timeout=60.0))
    assert d["fs_rt"] == "ok", d
    assert int(d["fs_rt_size"]) == 262144, d


def test_append_extends_rather_than_truncates(fsb):
    """Three opens with FILE_WRITE, three lines.

    This is the one that catches the classic-API shim mapping FILE_WRITE to
    "w". The file would still exist, still be readable, and contain exactly the
    last line -- which looks like a working filesystem right up until someone
    reads their log.
    """
    d = _kv(fsb["b"].command("fsappend", timeout=20.0))
    assert d["fs_append"] == "ok", d
    assert int(d["fs_append_lines"]) == 3, d


def test_seek_in_all_three_modes(fsb):
    d = _kv(fsb["b"].command("fsseek", timeout=20.0))
    assert d["fs_seek"] == "ok", d


def test_directories_listing_and_rename(fsb):
    d = _kv(fsb["b"].command("fsdirs", timeout=20.0))
    assert d["fs_dir"] == "ok", d
    assert int(d["fs_dir_count"]) == 2, d


def test_files_survive_an_unmount(fsb):
    """Write, unmount, remount, read.

    Without this, everything above passes on a driver that only ever reads back
    what it just wrote -- which is exactly what a filesystem is not.
    """
    d = _kv(fsb["b"].command("fspersist", timeout=40.0))
    assert d["fs_persist"] == "ok", d
    assert d["fs_persist_read"] == "persisted-42", d


def test_the_filesystem_did_not_leak(fsb):
    """FatFs allocates a FATFS per mount and a FIL per open file, and the FS
    API refcounts both. Eight open/write/close cycles must come back level."""
    def free():
        out = fsb["b"].command("fsheap", timeout=10.0)
        for line in out.splitlines():
            line = line.strip()
            if line.startswith("heap_free="):
                return int(line[10:])
        raise AssertionError(f"fsheap answered {out!r}")

    before = free()
    for _ in range(8):
        fsb["b"].command("fsrt 4096", timeout=30.0)
    after = free()
    assert before - after < 2048, (
        f"{before - after} bytes went missing over 8 file round trips")


def test_file_timestamps_come_from_the_rtc(fsb):
    """A file written after the clock is set carries the real date.

    FatFs stamps directory entries through get_fattime(), which goes through
    time(), which goes through the RTC -- and none of those three knows about
    the others. Without the clock every file on the card gets the same
    hard-coded date, which looks fine in a listing and makes "newest file"
    meaningless.
    """
    now = int(time.time())
    d = _kv(fsb["b"].command(f"fstime {now}", timeout=30.0))
    assert d["rtc_begin"] == "1", ("the LSE would not start", d)
    assert d["rtc_is_set"] == "1", d

    stamped = int(d["fs_file_time"])
    # FAT stores seconds in two-second units, so the stamp can be one second
    # behind. Anything more means it did not come from the clock.
    assert 0 <= now - stamped <= 3, (
        f"file stamped {stamped} ({d.get('fs_file_iso')}) for a write at "
        f"{now}", d)
