import pytest


def _kv(out):
    d = {}
    for line in out.splitlines():
        if "=" in line:
            k, _, v = line.partition("=")
            d[k.strip()] = v.strip()
    return d


# ---- Serial -------------------------------------------------------------

def test_echo(board):
    assert "echo:hello world" in board.command("echo hello world")


def test_print_formats(board):
    out = board.command("printtest")
    assert "int=42" in out
    assert "float=3.14" in out
    assert "str=abc" in out
    assert "hex=FF" in out


def test_baud_is_derived_from_hclk(board):
    assert _kv(board.command("serialinfo"))["hclk"] == "100000000"


# ---- The three structural defences --------------------------------------

def test_clock_enable_reads_back(board):
    """A wrong RCC bus reads back as zeroes with no error at all."""
    assert _kv(board.command("rcctest"))["rcc_readback"] == "ok"


def test_block_reset_refuses_the_shared_blocks(board):
    """GPIOx/AFIO are shared by every driver, PWR drops the VIO18 rail, DMA1's
    channels are handed out to several drivers, and resetting ETH hangs the
    boot in a way no delay fixes. All four must be refused."""
    assert _kv(board.command("rcctest"))["reset_refused"] == "4"


# ---- GPIO ---------------------------------------------------------------

def test_gpio_output_reads_back_through_the_jumper(board):
    d = _kv(board.command("gpiotest"))
    if d.get("jumper_pc3_pc4") == "0":
        pytest.skip("PC3-PC4 jumper not fitted -- a missing precondition, not a failure")
    assert d["gpio_loopback"] == "ok"


def test_input_pullup(board):
    d = _kv(board.command("gpiotest"))
    assert d["gpio_pullup"] == "1"


# ---- Interrupts ---------------------------------------------------------

def test_interrupt_fires(board):
    d = _kv(board.command("gpiotest"))
    if d.get("jumper_pc3_pc4") == "0":
        pytest.skip("PC3-PC4 jumper not fitted")
    assert _kv(board.command("irqtest"))["irq_count"] == "1"


def test_a_conflicting_exti_line_is_refused_not_stolen(board):
    """EXTI lines are shared by pin NUMBER across ports, so PA0 and PB0 cannot
    both have one. Stealing it would break a driver that is working."""
    d = _kv(board.command("irqconflict"))
    assert d["attach_second"] == "refused", d
    assert d["first_still_owns"] == "1", d


# ---- Analog -------------------------------------------------------------

def test_internal_reference_measures_vdda(board):
    """ADC1_IN17 is the internal 1.20 V reference. Reading it is the check on
    the assumption that VDDA is 3.3 V."""
    v = float(_kv(board.command("vref"))["vdda"])
    assert 3.0 < v < 3.6, f"VDDA measured {v} V"


def test_adc_clock_is_12mhz(board):
    """ADCPRE is written by hand: the SDK's constants sit at the wrong bit
    positions and disagree with the register's own mask."""
    assert _kv(board.command("adcinfo"))["adcclk"] == "12500000"


def test_analog_read_is_in_range(board):
    raw = int(_kv(board.command("adcread"))["a0_raw"])
    assert 0 <= raw <= 1023, raw
