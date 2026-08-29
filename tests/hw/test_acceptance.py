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


def test_flash_vs_itcm_ratio_is_recorded(board):
    """Records how much slower flash is than ITCM.

    Right now this is about 145x, because the V5F's instruction cache is off:
    it is disabled at reset (cache_strtg_ctlr, CSR 0xBC2, bit 1 ic_disable,
    reset value 1) and every attempt to enable it so far leaves the core
    trapping in startup. See startup_v5f.S and docs/hazards.md.

    The assertion is deliberately loose. This is a measurement, not a gate --
    it exists so the number is in front of whoever next tries to turn the cache
    on, and so a change that fixes it is impossible to miss.
    """
    d = _kv(board.command("bench", timeout=25.0))
    xip, itcm = int(d["xip_us"]), int(d["itcm_us"])
    assert xip > 0 and itcm > 0
    ratio = xip / itcm
    print(f"\nflash/ITCM = {ratio:.1f}x  (flash={xip} us, ITCM={itcm} us)")
    if ratio < 2.0:
        print("  -> the instruction cache appears to be ON. Update the docs.")
