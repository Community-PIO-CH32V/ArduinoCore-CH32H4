import time


def _num(out, key):
    for line in out.splitlines():
        if line.startswith(key + "="):
            return int(line.split("=", 1)[1].strip())
    raise AssertionError(f"{key} not in {out!r}")


def test_millis_matches_the_host_clock(board):
    """Measured against the HOST's clock, not a number the board reported about
    itself. A factor-of-four clock error is invisible to any test that only
    compares the board with itself."""
    t0 = time.monotonic()
    a = _num(board.command("millis"), "millis")
    time.sleep(2.0)
    b = _num(board.command("millis"), "millis")
    t1 = time.monotonic()
    host_ms = (t1 - t0) * 1000.0
    assert abs((b - a) - host_ms) < host_ms * 0.02, f"board {b-a} ms vs host {host_ms:.0f} ms"


def test_delay_is_accurate(board):
    """SysTick counts at HCLK (100 MHz), not the V5F's 400 MHz core clock."""
    us = _num(board.command("delaytest", timeout=5.0), "delay1000_us")
    assert 990_000 < us < 1_010_000, us


def test_micros_is_monotonic(board):
    assert "micros_monotonic=ok" in board.command("microstest", timeout=5.0)


def test_delaymicroseconds_is_accurate(board):
    """1000 x 100 us should be 100 ms, allowing for per-call overhead."""
    us = _num(board.command("delayustest", timeout=5.0), "delayus_total")
    assert 100_000 <= us < 115_000, us
