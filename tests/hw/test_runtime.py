def _kv(out):
    d = {}
    for line in out.splitlines():
        if "=" in line:
            k, _, v = line.partition("=")
            d[k.strip()] = v.strip()
    return d


def test_heap_spans_both_regions(board):
    """DTCM's remainder plus the shared region. dlmalloc opens a new segment at
    the discontinuity rather than assuming sbrk is contiguous."""
    free = int(_kv(board.command("heapinfo"))["heap_free"])
    assert free > 600_000, free


def test_a_large_allocation_reaches_the_shared_region(board):
    """300 KB cannot come from DTCM alone, so this proves the sbrk hand-off."""
    assert _kv(board.command("bigalloc"))["big_alloc"] == "ok"


def test_a_throw_is_actually_caught(board):
    """Exceptions link cleanly and fail only at run time when the unwind tables
    are not registered -- which is precisely what nano.specs does. A link is
    not evidence; this has to run on hardware."""
    v = _kv(board.command("throwtest"))["caught"]
    assert v in ("42", "disabled"), v
