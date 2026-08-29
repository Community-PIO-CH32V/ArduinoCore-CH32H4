"""USB CDC.

`Serial` is the CDC device by default; `Serial1` is USART1, which is what the
rest of the suite talks to. These tests prove the USB path end to end rather
than merely that the stack initialised.
"""
import time

import pytest

try:
    import serial
    import serial.tools.list_ports as list_ports
except ImportError:  # pragma: no cover
    serial = None

CDC_VID_PID = "1209:0001"


def _kv(out):
    d = {}
    for line in out.splitlines():
        if "=" in line:
            k, _, v = line.partition("=")
            d[k.strip()] = v.strip()
    return d


def _find_cdc_port(timeout=10.0):
    """Wait for the CDC port to appear.

    The session fixture resets the board, which drops the USB device and makes
    the host tear the port down and build it again. That takes a couple of
    seconds and is not instantaneous after the reset returns, so polling here
    is the difference between testing USB and testing how fast Windows is."""
    deadline = time.time() + timeout
    while True:
        for p in list_ports.comports():
            if p.hwid and CDC_VID_PID.lower() in p.hwid.lower():
                return p.device
        if time.time() >= deadline:
            return None
        time.sleep(0.5)


def test_the_stack_comes_up(board):
    assert "V5F: usb up" in board.banner, board.banner


def test_usb_refuses_to_start_without_the_crystal(board):
    """Full-speed USB needs a 0.25%-accurate clock, which an on-chip RC cannot
    provide. The core reports rather than enumerating something that half
    works -- and the failure would be a device that detects bus reset and
    suspend (DC conditions, no clock needed) and never decodes a packet."""
    assert "usb DOWN" not in board.banner


def test_device_reports_itself_active(board):
    assert _kv(board.command("usbstat"))["usb_active"] == "1"


def test_the_device_enumerates(board):
    """A CDC port with our VID:PID must exist on the host.

    Not 1A86:8010 -- those are the WCH-LinkE's own, and a device sharing them
    inherits the probe's driver binding, enumerates perfectly and never becomes
    a COM port."""
    port = _find_cdc_port()
    assert port is not None, (
        f"no {CDC_VID_PID} port found. Is the board's USB-C plugged into this "
        "host? Ports seen: "
        + ", ".join(f"{p.device}({p.hwid})" for p in list_ports.comports()))


def test_host_can_open_the_cdc_port(board):
    port = _find_cdc_port()
    if port is None:
        pytest.skip("no CDC port")
    with serial.Serial(port, 115200, timeout=1) as s:
        assert s.is_open


def test_device_to_host(board):
    """The board writes a known string out of the CDC port."""
    port = _find_cdc_port()
    if port is None:
        pytest.skip("no CDC port")
    with serial.Serial(port, 115200, timeout=2) as s:
        s.reset_input_buffer()
        assert _kv(board.command("usbwrite"))["usb_wrote"] == "1"
        deadline, got = time.time() + 3.0, ""
        while time.time() < deadline:
            chunk = s.read(s.in_waiting or 1)
            if chunk:
                got += chunk.decode(errors="replace")
            if "hello-from-usb" in got:
                break
        assert "hello-from-usb" in got, repr(got)


def test_host_to_device_round_trip(board):
    """loop() echoes CDC input straight back, so this exercises read and write
    on the device side and both directions on the wire."""
    port = _find_cdc_port()
    if port is None:
        pytest.skip("no CDC port")
    with serial.Serial(port, 115200, timeout=2) as s:
        s.reset_input_buffer()
        s.write(b"round-trip-probe\n")
        s.flush()
        deadline, got = time.time() + 3.0, ""
        while time.time() < deadline:
            chunk = s.read(s.in_waiting or 1)
            if chunk:
                got += chunk.decode(errors="replace")
            if "round-trip-probe" in got:
                break
        assert "round-trip-probe" in got, repr(got)
