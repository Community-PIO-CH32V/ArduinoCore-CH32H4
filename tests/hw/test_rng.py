"""The hardware entropy source, and the whitening it is not usable without.

The raw peripheral is a limited value pool. Nothing about it says so: its own
status register reports data-ready with no seed error and no clock error the
whole time, so a driver that hands raw words straight out looks correct, passes
any test that only asks "is it changing", and quietly supplies about ten bits
of entropy per word to whatever is built on top of it.

These tests exist to keep that measurement attached to the code.
"""


def _kv(out):
    d = {}
    for line in out.splitlines():
        for part in line.split():
            if "=" in part:
                k, _, v = part.partition("=")
                d[k.strip()] = v.strip()
    return d


def test_the_generator_reports_itself_healthy(board):
    """A clock out of range is the one failure software cannot recover from,
    and it is latched rather than papered over."""
    d = _kv(board.command("rngtest", timeout=25.0))
    assert d["rng_ok"] == "1", d


def test_raw_output_is_not_uniform(board):
    """The measurement that justifies the whitening.

    600 raw draws from a uniform 32-bit source would collide with probability
    about 4e-5 -- essentially all 600 distinct. This part returns roughly 400
    to 500, and the count saturates rather than growing with the sample size,
    so it is a limited pool and not merely reading faster than the generator
    refreshes.

    Asserting the DEFECT looks odd until you consider what the alternative
    catches: if a future silicon revision or clock change fixes the source,
    this fails, and the whitening should then be re-justified rather than
    carried forever on a comment describing hardware that no longer behaves
    that way. It is a bound on what is known, in both directions.
    """
    d = _kv(board.command("rngtest", timeout=25.0))
    n, raw = int(d["rng_n"]), int(d["rng_raw_distinct"])
    assert raw > n // 4, ("the raw source has collapsed to almost no values, "
                          "which is worse than documented", d)
    assert raw < n, ("the raw source now looks uniform -- re-check whether the "
                     "whitening in ch32h4_rng.c is still needed, and why", d)


def test_whitened_output_is_uniform(board):
    """600 of 600 distinct, every time.

    This is the assertion that would have caught handing raw words out: the
    same 600 draws through the pool are all distinct, where the source under
    them is not.
    """
    d = _kv(board.command("rngtest", timeout=25.0))
    n, mixed = int(d["rng_n"]), int(d["rng_mixed_distinct"])
    assert mixed == n, (f"{n - mixed} collisions in {n} whitened draws", d)


def test_bytes_are_spread_across_the_range(board):
    """1024 bytes, bucketed by high nibble: 64 expected per bucket.

    A dead peripheral gives one bucket 1024 and the rest zero, which every
    "is it changing" test in the world would still pass if it only looked at
    consecutive words.
    """
    d = _kv(board.command("rngbytes", timeout=15.0))
    lo, hi = int(d["rng_nibble_min"]), int(d["rng_nibble_max"])
    # Multinomial spread over 16 buckets: 64 +/- 8 is one sigma, so these
    # bounds are wide enough never to flake and tight enough to catch a
    # constant, a stuck bit, or a source with no high nibble.
    assert lo >= 30, (f"a nibble value appeared only {lo} times in 1024", d)
    assert hi <= 110, (f"a nibble value appeared {hi} times in 1024", d)
