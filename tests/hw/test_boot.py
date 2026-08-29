def test_board_announces_itself(board):
    assert "CH32H4 Arduino core" in board.banner


def test_clock_source_is_the_crystal(board):
    """On the internal RC the board runs, but the Ethernet PLL never locks and
    USB is out of spec. Everything downstream of that presents as a different
    bug, so it is asserted here rather than discovered in M6."""
    assert "sysclk_src=hse" in board.banner, \
        "fell back to the internal RC -- check the crystal"


def test_hclk_is_100mhz(board):
    """SystemInit must actually run. Without it the part stays on the 70 MHz
    bootstrap PLL, which is plausible enough to pass a self-consistent test."""
    assert "hclk=100000000" in board.banner


def test_sysclk_is_400mhz(board):
    assert "sysclk=400000000" in board.banner


def test_no_degraded_clock_warning(board):
    assert "WARNING: degraded clock" not in board.banner


def test_v5f_entry_agrees_with_the_linker(board):
    """The build constant and the linker script are two statements of one
    address, and nothing else compares them."""
    assert "FATAL: _v5f_entry" not in board.banner
