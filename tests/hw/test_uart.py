"""The USARTs beyond Serial1, on hardware.

NEEDS ONE JUMPER: PA2 to PA3 -- USART2's default TX to its own RX. Without it
the round-trips report zero bytes and these skip, because an unwired bench is a
missing precondition rather than a broken driver.

Loopback on a single peripheral is the strongest cheap test available here. It
runs the entire path -- the pin multiplexer, the baud divisor, the transmit
shift register, the wire, receive, the RXNE interrupt and the ring buffer --
and the failure mode this core is most exposed to, a wrong alternate function
number, produces silence rather than a wrong answer. Nothing short of moving
real bytes distinguishes "configured" from "working": with the wrong AF the
USART still reports TXE and TC set and BRR still holds the right divisor.
"""
import re

import pytest

from conftest import Reply


def kv(text):
    out = {}
    for line in text.splitlines():
        m = re.match(r"^([a-z0-9_]+)=(-?\d+)$", line.strip())
        if m:
            out[m.group(1)] = int(m.group(2))
    return Reply(text, out)


@pytest.fixture(scope="module")
def wired(uart_board):
    """Skip the whole module if the loopback jumper is absent."""
    d = kv(uart_board.command("uartloop 115200", timeout=10))
    if d.get("uart_got", 0) == 0:
        pytest.skip("no bytes returned on USART2; is PA2 jumpered to PA3?")
    return uart_board


def test_the_port_knows_which_usart_it_is(uart_board):
    d = kv(uart_board.command("uartinfo", timeout=5))
    assert d["uart_id"] == 2, d.raw


def test_a_pin_that_cannot_carry_the_signal_is_refused(uart_board):
    """setTX() must reject rather than accept and ignore.

    A pin with no alternate function for this USART gives a port whose every
    status flag reads correctly and whose output never leaves the die. Refusing
    is the only way a sketch can find that out.
    """
    d = kv(uart_board.command("uartinfo", timeout=5))
    assert d["uart_good_pin"] == 1, d.raw
    assert d["uart_bad_pin_refused"] == 1, d.raw


@pytest.mark.parametrize("baud", [9600, 115200, 921600])
def test_bytes_survive_a_round_trip(wired, baud):
    """Three rates an order of magnitude apart.

    The divisor comes from HCLK, not SystemCoreClock -- four times higher on
    this core -- so a rate that is wrong by that factor is the mistake to
    catch, and it shows up at every rate rather than none.
    """
    d = kv(wired.command("uartloop %d" % baud, timeout=15))
    assert d["uart_got"] == 30, d.raw
    assert d["uart_match"] == 1, d.raw


def test_even_parity_round_trips(wired):
    d = kv(wired.command("uartparity", timeout=15))
    assert d["uart_match"] == 1, d.raw


def test_two_stop_bits_round_trip(wired):
    d = kv(wired.command("uartstop2", timeout=15))
    assert d["uart_match"] == 1, d.raw


def test_overflow_drops_rather_than_wrapping(wired):
    """400 bytes into a 256-byte buffer.

    Bounded is the requirement. A ring that wraps past its tail keeps handing
    back bytes, from the wrong end of the stream, and a sketch reading them
    sees plausible data in the wrong order -- far worse than a gap.
    """
    d = kv(wired.command("uartoverflow", timeout=20))
    assert d["uart_overflow_bounded"] == 1, d.raw
    assert 0 < d["uart_overflow"] <= 255, d.raw
