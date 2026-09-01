"""The milestone's acceptance criteria."""


def _kv(out):
    d = {}
    for line in out.splitlines():
        for part in line.split():
            if "=" in part:
                k, _, v = part.partition("=")
                d[k.strip()] = v.strip()
    return d


def test_blink_toggles_the_pin(board):
    """Ten writes alternating from the pin's existing LOW state produce nine
    transitions, not ten -- the first write is a no-op."""
    assert "blink_transitions=9" in board.command("blinktest", timeout=5.0)


def test_serial_echo_round_trips(board):
    assert "echo:the quick brown fox" in board.command("echo the quick brown fox")


def test_instruction_cache_is_on(board):
    """Flash must run at roughly ITCM speed. This is a gate, not a note.

    With the V5F instruction cache enabled and scoped to the PMP window over
    .text/.rodata, a flash-resident loop measures about 0.9x an identical loop
    in ITCM -- flash is not slower in any way that shows. With the cache off it
    is about 145x slower.

    No register distinguishes the two. pmpcfg0, cache_pmp_ovr and
    cache_strtg_ctlr all read back exactly what was written whether or not the
    cache is covering anything, so a wrong bit in the enable sequence produces
    a board that boots, passes every functional test, and runs two orders of
    magnitude slow. That has happened once already: IC_Str was moved from bit 5
    to bit 6 on the strength of the QingKeV5 manual's table 4-3, and nothing
    caught it. Hence the gate.

    The threshold is 3x -- far above the ~0.9x of a working cache and far below
    the ~145x of a broken one, so it cannot fail for jitter.
    """
    d = _kv(board.command("bench", timeout=30.0))
    xip, itcm = int(d["xip_us"]), int(d["itcm_us"])
    assert xip > 0 and itcm > 0
    ratio = xip / itcm
    print(f"\nflash/ITCM = {ratio:.2f}x  (flash={xip} us, ITCM={itcm} us)")
    assert ratio < 3.0, (
        f"flash is {ratio:.1f}x slower than ITCM -- the instruction cache is "
        "off. Check PMPCFG_IC_STR and the csrc of cache_strtg_ctlr in "
        "startup_v5f.S.")
