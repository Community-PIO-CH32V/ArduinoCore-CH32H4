"""The clock matrix behind the 25 MHz default. Not run by default.

`docs/qspi-read-timing.md` records that the ceiling is NOT a simple frequency
limit -- reads fail at divider 3 but pass at 2 and 1, writes fail only at
divider 2 -- and that any future attempt to raise the clock needs the whole
matrix rather than one sketch that appears to work. This is that matrix.

It is skipped by default because it takes minutes, deliberately drives
configurations known to corrupt data, and reports rather than asserts. The
gate is an environment variable rather than a marker because this repository
has no pytest config to register one in, and an unregistered marker would not
actually deselect anything. Run it by hand:

    PSRAM_CLOCK_MATRIX=1 WLINK=tools/bin/wlink.exe \
        python -m pytest tests/hw/test_psram_clock_matrix.py -q -s

Read the output; there is nothing here for CI to pass or fail.
"""
import os
import re

import pytest

from conftest import kv

pytestmark = pytest.mark.skipif(
    not os.environ.get("PSRAM_CLOCK_MATRIX"),
    reason="clock-matrix diagnostic; set PSRAM_CLOCK_MATRIX=1 to run")

# Requested Hz chosen so the integer prescaler lands on the divider we want.
# begin() rounds the divider UP so the clock never exceeds the request, which
# is why 33 MHz would silently give 25 MHz and 34 MHz gives 33.3.
LADDER = [
    (13000000, 8, "12.5 MHz"),
    (17000000, 6, "16.7 MHz"),
    (20000000, 5, "20.0 MHz"),
    (25000000, 4, "25.0 MHz"),
    (34000000, 3, "33.3 MHz"),
    (50000000, 2, "50.0 MHz"),
    (100000000, 1, "100.0 MHz"),
]


def _parse(text):
    rows, meta = {}, {}
    for line in text.splitlines():
        m = re.match(r"^l(\d+)=(\d+),(\d+),(\d+)$", line.strip())
        if m:
            rows[int(m.group(1))] = tuple(int(m.group(i)) for i in (2, 3, 4))
            continue
        m = re.match(r"^(sweep_\w+)=(\S+)$", line.strip())
        if m:
            meta[m.group(1)] = m.group(2)
    return meta, rows


def _report(label, text):
    meta, rows = _parse(text)
    print(f"\n=== {label} ===")
    print(f"    wclk={meta.get('sweep_wclk')} rclk={meta.get('sweep_rclk')} "
          f"dir={meta.get('sweep_dir')} done={meta.get('sweep_done')}")
    if not rows:
        print("    NO ROWS: " + text.replace("\n", " | ")[:300])
        return
    print("    len | bad reps/8 | bad bytes | short reads")
    for n in sorted(rows):
        br, bb, sr = rows[n]
        print(f"    {n:5d} | {br:10d} | {bb:9d} | {sr:11d}")


@pytest.mark.parametrize("hz,div,label", LADDER)
def test_read_ladder(psram_board, hz, div, label):
    """Short transfers, 1 to 1024 bytes, read at the test clock."""
    _report(f"READ at {label} (div {div})",
            psram_board.command(f"sweep {hz}", timeout=90))


@pytest.mark.parametrize("hz,div,label", [r for r in LADDER if r[1] <= 4])
def test_write_ladder(psram_board, hz, div, label):
    """The same, with only the write side at the test clock. A write has no
    round trip, so a failure here means something other than return timing."""
    _report(f"WRITE at {label} (div {div})",
            psram_board.command(f"sweep {hz} w", timeout=90))


@pytest.mark.parametrize("hz,div,label", [r for r in LADDER if r[1] <= 4])
def test_streamed_burst(psram_board, hz, div, label):
    """64 KB read as one stream, which is the path that actually ships.

    This is the case the short-transfer sweep misses: divider 3 is clean here
    and dirty there, at the same clock.
    """
    r = kv(psram_board.command(f"speed {hz}", timeout=90))
    us = r["speed_us"]
    print(f"\n  {label:>10} (div {div}) | reported {r['speed_clock']:>9} Hz "
          f"| {us:6d} us | {65536.0 / us:6.2f} MB/s "
          f"| bad words {r['speed_bad']} / 16384")
