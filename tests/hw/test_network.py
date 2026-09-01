"""TCP and UDP, against this machine rather than against the board itself.

A loopback test would prove the classes compile and that lwIP talks to itself.
It would not prove a frame ever reached the wire, which is the part that was
actually in doubt: the MAC, the PHY, the descriptor rings, and the pumping of
the stack from yield(). So every test here uses a real socket on the host, and
the board has to get a DHCP lease first or they all skip.

Requires the board's RJ45 on the same network as this machine.
"""
import socket
import threading
import time

import pytest


def _kv(out):
    """key=value pairs out of a reply. Every line is stripped first: the
    console prompt is "> " with no newline after it, so a stray leading space
    at the head of a line is normal here."""
    d = {}
    for line in out.splitlines():
        for part in line.strip().split():
            if "=" in part:
                k, _, v = part.partition("=")
                d[k.strip()] = v.strip()
    return d


def _board_ip(banner: str) -> str:
    # Every line is stripped first. The console prompt is "> " with no newline
    # after it, so a stray leading space at the head of a line is normal here
    # and an unstripped match silently finds nothing.
    for line in banner.splitlines():
        line = line.strip()
        if line.startswith("ip="):
            return line[3:].strip()
    return ""


@pytest.fixture(scope="module")
def net(ethernet_board):
    """The board's address, and this machine's address as the board sees it.

    Skips rather than fails without a lease: no DHCP server on the bench is a
    missing precondition, not a broken core, and the two must not look alike.
    """
    banner = ethernet_board.banner
    ip = _board_ip(banner)
    if not ip or ip == "0.0.0.0":
        pytest.skip("the board has no DHCP lease -- is the RJ45 plugged in?\n"
                    + banner[-800:])

    # Which of this machine's addresses the board would reach. Connecting a UDP
    # socket assigns a local address without sending anything.
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.connect((ip, 7001))
    host = probe.getsockname()[0]
    probe.close()
    return {"board": ip, "host": host, "b": ethernet_board}


def test_dhcp_lease_is_complete(ethernet_board):
    """An address alone is not a working configuration -- a sketch that gets an
    IP and no gateway reaches its own subnet and nothing else."""
    banner = ethernet_board.banner
    assert "dhcp=1" in banner, banner
    assert "ip=0.0.0.0" not in banner, banner
    assert "gw=0.0.0.0" not in banner, banner
    assert "mask=0.0.0.0" not in banner, banner


def test_the_mac_address_is_not_all_zeroes(ethernet_board):
    """Derived from the chip's unique ID. All zeroes, or the same on every
    board, means the derivation silently did nothing -- which works on a bench
    with one board and fails the moment there are two."""
    line = next(l.strip() for l in ethernet_board.banner.splitlines()
                if l.strip().startswith("mac="))
    mac = line[4:].strip()
    assert mac.count(":") == 5, mac
    assert mac != "00:00:00:00:00:00", mac
    assert mac != "FF:FF:FF:FF:FF:FF", mac


def test_server_echoes_a_short_message(net):
    """EthernetServer accepts, EthernetClient reads and writes."""
    s = socket.create_connection((net["board"], 7000), timeout=5)
    s.settimeout(5)
    try:
        payload = b"the quick brown fox jumps over the lazy dog\n"
        s.sendall(payload)
        got = b""
        deadline = time.time() + 5
        while len(got) < len(payload) and time.time() < deadline:
            got += s.recv(4096)
        assert got == payload, got
    finally:
        s.close()


def test_server_echoes_more_than_one_segment(net):
    """10 KB, which is seven MSS-sized segments.

    A single short message fits in one segment and never exercises the window,
    the sent callback, or tcp_write returning ERR_MEM. Bulk does all three, and
    a receive path that loses a descriptor shows up here and nowhere else.
    """
    s = socket.create_connection((net["board"], 7000), timeout=5)
    s.settimeout(15)
    try:
        big = bytes(range(256)) * 40
        s.sendall(big)
        got = b""
        deadline = time.time() + 20
        while len(got) < len(big) and time.time() < deadline:
            chunk = s.recv(65536)
            if not chunk:
                break
            got += chunk
        assert len(got) == len(big), f"{len(got)} of {len(big)} came back"
        assert got == big, "the echo came back corrupted or reordered"
    finally:
        s.close()


def test_board_connects_out_and_round_trips(net):
    """The direction the echo server does not cover.

    EthernetClient::connect has to complete its three-way handshake through
    callbacks that only run when yield() pumps the stack -- a connect() that
    waits without pumping cannot ever succeed, and that is exactly the shape a
    sketch would write by hand.
    """
    srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
    srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
    srv.bind(("0.0.0.0", 7100))
    srv.listen(1)
    srv.settimeout(15)

    received = []

    def serve():
        try:
            conn, addr = srv.accept()
            conn.settimeout(5)
            received.append((addr, conn.recv(4096)))
            conn.sendall(b"pong from host\n")
            time.sleep(0.3)
            conn.close()
        except Exception as exc:            # surfaced through the assertions
            received.append(("error", repr(exc).encode()))

    th = threading.Thread(target=serve, daemon=True)
    th.start()
    time.sleep(0.3)
    try:
        out = net["b"].command(f"tcpget {net['host']} 7100", timeout=10.0)
        th.join(timeout=10)

        assert "tcp_connect=1" in out, out
        assert "tcp_reply=pong from host" in out, out
        assert "tcp_closed=1" in out, out
        assert received, "the host never saw the connection"
        assert received[0][1] == b"hello from ch32h417\n", received
        assert received[0][0][0] == net["board"], received
    finally:
        srv.close()


def test_udp_round_trips_in_both_directions(net):
    """One socket, both ways, because a datagram carries its own peer.

    parsePacket() must make exactly one datagram current and remoteIP()/
    remotePort() must describe that one -- the board replies to whatever
    address it read out of the packet, so a socket that kept the address of an
    earlier packet would answer the wrong host and this would fail.
    """
    u = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    u.settimeout(5)
    u.bind(("0.0.0.0", 7101))
    try:
        msg = b"udp round trip test 12345"
        u.sendto(msg, (net["board"], 7001))
        back, frm = u.recvfrom(2048)
        assert back == msg, back
        assert frm[0] == net["board"], frm

        out = net["b"].command(f"udpsend {net['host']} 7101", timeout=5.0)
        assert "udp_send=1" in out, out
        back, frm = u.recvfrom(2048)
        assert back == b"hello udp from ch32h417", back
    finally:
        u.close()


def test_the_receive_path_lost_nothing(net):
    """Counters after the traffic above.

    A dropped frame is invisible to TCP -- it retransmits and the test still
    passes, just slower. These are the only place a receive path that is
    quietly running out of descriptors shows up.
    """
    out = net["b"].command("netstat", timeout=5.0)
    d = {}
    for line in out.splitlines():
        if "=" in line:
            k, _, v = line.partition("=")
            d[k.strip()] = v.strip()
    assert int(d["rx_frames"]) > 0, d
    assert int(d["tx_frames"]) > 0, d
    assert int(d["rx_dropped"]) == 0, ("frames arrived with no pbuf to put "
                                       "them in", d)
    assert int(d["rx_buf_unavail"]) == 0, ("the DMA ran out of descriptors", d)
    assert int(d["tx_errors"]) == 0, d


def test_the_stack_did_not_leak(net):
    """Connections come and go; the heap must come back.

    Every context, pbuf and pcb above was allocated and freed. A leak of a few
    hundred bytes per connection is invisible on a bench and fatal in a week.
    """
    def free():
        # 5 s, not 3: the board is still retiring the connections this test
        # just closed, and a marginal timeout here reads as heapinfo having
        # produced no answer at all.
        out = net["b"].command("heapinfo", timeout=5.0)
        # strip(): the tail of the previous prompt -- a bare space -- can
        # arrive just after reset_input_buffer() and land at the head of the
        # first line, so an unstripped startswith() misses the answer roughly
        # one run in three.
        for l in out.splitlines():
            if l.strip().startswith("heap_free="):
                return int(l.strip()[10:])
        raise AssertionError(f"heapinfo answered {out!r}")

    before = free()
    for _ in range(8):
        s = socket.create_connection((net["board"], 7000), timeout=5)
        s.settimeout(5)
        s.sendall(b"leak check\n")
        s.recv(64)
        s.close()
        time.sleep(0.1)
    net["b"].command("netstat", timeout=3.0)   # let the closes settle
    after = free()

    # TIME_WAIT pcbs are still held here, so an exact match is not the bar.
    # Eight connections leaking a context each would be thousands of bytes.
    assert before - after < 2048, (
        f"{before - after} bytes went missing over 8 connections")


def test_sntp_sets_the_clock_from_the_network(net):
    """A real NTP round trip, from a clock that was deliberately cleared first.

    Clearing matters. NTP.waitSynced() returns immediately when the clock is
    already known -- which is right for a sketch and useless for a test, since
    it passes in three milliseconds without a datagram leaving the board. The
    sketch resets the counter by selecting a different RTC source and back,
    then waits for an actual answer.

    Needs a route to the internet. Skips without one rather than failing: a
    bench with no default route is a missing precondition, not a broken stack.
    """
    out = net["b"].command("ntpsync pool.ntp.org", timeout=40.0)
    d = _kv(out)
    assert d.get("rtc_begin") == "1", ("the LSE would not start", out)
    assert d.get("rtc_was_set") == "0", ("the clock was not cleared, so this "
                                         "would pass without a sync", out)
    if d.get("ntp_begin") != "1" or d.get("ntp_answer") != "1":
        pytest.skip(f"no NTP answer -- is there a route to the internet? {out}")

    board = int(d["ntp_unix"])
    host = int(time.time())
    assert abs(board - host) < 10, (
        f"board says {board} ({d.get('ntp_iso')}), host says {host}", out)
    # A real round trip takes tens of milliseconds at least. Anything
    # instantaneous means the clock was already set and nothing was measured.
    assert int(d["ntp_ms"]) > 10, ("suspiciously fast -- did a packet really "
                                   "go out?", out)


def test_sntp_refuses_to_start_with_no_server(net):
    """No server configured and none offered by DHCP is an error, not a
    silent poll of address zero forever."""
    out = net["b"].command("ntpsync", timeout=30.0)
    d = _kv(out)
    if d.get("ntp_begin") == "1":
        # This network's DHCP does offer one, which is also correct.
        assert d.get("ntp_answer") == "1", ("DHCP offered a server but no "
                                            "answer came back", out)
    else:
        assert d.get("ntp_begin") == "0", out
