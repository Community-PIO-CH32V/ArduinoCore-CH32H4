"""The fault handler.

A fault during static initialisation is otherwise completely silent:
constructors run before the sketch calls Serial.begin(), so a handler that
assumed the console driver had run would print into a UART that was never
configured, and the board would emit nothing at all -- indistinguishable from
a dead chip, a bad flash or a wedged probe. This handler brings up its own
UART when it has to, and leaves a working one alone.
"""


def test_a_trap_prints_a_register_dump(board):
    out = board.command("crash", timeout=4.0)
    try:
        assert "=== TRAP ===" in out, out
        assert "mcause=0x00000002" in out, out   # illegal instruction
        assert "mepc=" in out and "mtval=" in out, out
        assert "illegal instruction" in out, out
    finally:
        board.reboot()


def test_the_handler_dumps_once_and_resets(board):
    """It must dump exactly once and then reset -- not spin.

    Interrupts are disabled before the dump, or SysTick keeps firing into the
    handler, each one nesting on the last, and the dump scrolls away.

    Resetting rather than halting is deliberate, and it is not a style choice.
    A core sitting in a spin loop with interrupts off wedges the WCH-Link with
    a 0x55 protocol error, and the only way back is holding NRST down through
    an erase. A fault that needs physical intervention to clear is worse than
    one that reboots -- so the handler prints, waits for the UART to drain, and
    resets. The record survives in .xcore and the V3F prints it on the way back
    up, which is how a fault too severe to print at all still gets reported.
    """
    out = board.command("crash", timeout=6.0)
    try:
        assert out.count("=== TRAP ===") == 1, ("handler re-entered", out)
        assert "CH32H4 Arduino core" in out, ("did not reset", out)
        assert "v5f fault: mcause=0x00000002" in out, ("the V3F did not replay "
                                                      "the record", out)
    finally:
        board.reboot()


def test_the_replayed_record_carries_the_interrupt_trace(board):
    """The V3F's replay must carry real counters, not whatever .xcore held.

    The counter fields are filled by the dumper, not by the naked entry -- that
    one runs without a usable stack and does only what PC-relative stores can
    do. When they were left unfilled the replay printed eight-digit nonsense
    that read exactly like an interrupt storm.
    """
    out = board.command("crash", timeout=6.0)
    try:
        line = next(l for l in out.splitlines() if l.startswith("v5f fault:"))
        trace = out.splitlines()[out.splitlines().index(line) + 1]
        assert trace.startswith("irq eth="), (trace, out)
        counts = dict(p.split("=") for p in trace.split() if "=" in p)
        # A crash a second or so into the sketch: SysTick has fired hundreds of
        # times, not billions, and nothing was nesting.
        assert 1 <= int(counts["systick"]) < 1_000_000, (counts, out)
        assert int(counts["max_nesting"]) <= 4, (counts, out)
    finally:
        board.reboot()
