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


def test_the_handler_halts_rather_than_looping(board):
    """It must stop, not scroll. Interrupts are disabled before the spin, or
    SysTick keeps nesting until the hardware stack overflows and the part
    resets -- which looks like a boot loop and scrolls the dump away."""
    out = board.command("crash", timeout=4.0)
    try:
        assert "halted" in out, out
        assert out.count("=== TRAP ===") == 1, "handler re-entered"
        assert "CH32H4 Arduino core" not in out, "the board reset instead of halting"
    finally:
        board.reboot()
