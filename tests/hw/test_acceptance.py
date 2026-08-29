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


def test_xip_runs_at_itcm_speed(board):
    """The V5F's instruction cache is DISABLED at reset (cache_strtg_ctlr, CSR
    0xBC2, bit 1 ic_disable, reset value 1). With it off, code in flash runs at
    1/145th of ITCM speed and the whole XIP-primary memory strategy collapses.
    With it on they are indistinguishable.

    This test is the regression guard on that one CSR write."""
    d = _kv(board.command("bench", timeout=20.0))
    xip, itcm = int(d["xip_us"]), int(d["itcm_us"])
    assert xip > 0 and itcm > 0
    ratio = xip / itcm
    print(f"\nXIP/ITCM = {ratio:.2f}  (xip={xip} us, itcm={itcm} us)")
    assert ratio < 2.0, (
        f"flash is {ratio:.0f}x slower than ITCM -- the instruction cache is "
        "probably off again; see startup_v5f.S")
