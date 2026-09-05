"""HTTPS served BY the board: EthernetServerSecure and WebServerSecure.

The client tests in test_tls.py stand a Python TLS server up and point the
board at it. This is the mirror image, and it is the harder direction: a client
that gets the handshake subtly wrong usually still fails to connect, while a
server that gets it wrong can hand out a connection that a lenient peer
accepts. So the peer here is Python's ssl module, which is OpenSSL -- an
implementation that shares no code with mbedtls and no assumptions with us.

WHAT IS ACTUALLY BEING CHECKED, beyond "a page came back":

  * the certificate the board presents verifies against the CA it was built
    with, and the hostname in it is checked rather than ignored;
  * a client that trusts a DIFFERENT CA is refused -- which is the test a
    server that presents garbage would also pass, if it were not paired with
    the one above;
  * a response larger than MBEDTLS_SSL_OUT_CONTENT_LEN comes back whole, so
    the write loop keeps feeding mbedtls after a partial write;
  * mutual TLS: with requireClientCert(true) a client with no certificate is
    refused and one with the right certificate is served;
  * the session heap comes back afterwards, because a leak of an mbedtls
    context is tens of kilobytes and four requests would be visible.

The certificates are the committed ones under tests/sketches/tlsserver/certs,
the same files the sketch's certs.h was generated from -- if those two ever
drift apart every test here fails at the handshake, which is the right way for
that to show up.
"""
import socket
import ssl
import time
from pathlib import Path

import pytest

CERTS = (Path(__file__).resolve().parents[1]
         / "sketches" / "tlsserver" / "certs")

SERVER_CA = CERTS / "server.crt"     # self-signed, so it is its own anchor
CLIENT_CA = CERTS / "clientca.crt"   # signs client.crt; the board trusts it
CLIENT_CRT = CERTS / "client.crt"
CLIENT_KEY = CERTS / "client.key"

# The name in the certificate's SAN. The board's address comes from DHCP, so
# the certificate cannot name it; the client connects by address and passes
# this as server_hostname, which is both what SNI carries and what the
# certificate is checked against.
SERVER_NAME = "ch32h4.local"

PORT = 443


def _kv(out):
    d = {}
    for line in out.splitlines():
        line = line.strip()
        if "=" in line:
            k, _, v = line.partition("=")
            d[k.strip()] = v.strip()
    return d


@pytest.fixture(scope="module")
def tls_server(tls_server_board):
    """The board, with an address, serving HTTPS."""
    banner = tls_server_board.banner
    ip = _kv(banner).get("tls_ip", "")
    if not ip or ip == "0.0.0.0":
        pytest.skip("the board has no DHCP lease -- is the RJ45 plugged in?")
    return tls_server_board, ip


def _context(ca=SERVER_CA, client_cert=False):
    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_CLIENT)
    ctx.load_verify_locations(str(ca))
    ctx.check_hostname = True
    ctx.verify_mode = ssl.CERT_REQUIRED
    if client_cert:
        ctx.load_cert_chain(str(CLIENT_CRT), str(CLIENT_KEY))
    return ctx


def _get(ip, path, ctx=None, timeout=25.0):
    """One HTTPS GET, on its own connection. Returns (status, body).

    Deliberately not http.client or requests: this has to be able to report a
    handshake failure as an exception from a known line, and a connection
    reused between tests would hide exactly the per-connection handshake this
    is testing.
    """
    ctx = ctx or _context()
    raw = socket.create_connection((ip, PORT), timeout=timeout)
    raw.settimeout(timeout)
    with ctx.wrap_socket(raw, server_hostname=SERVER_NAME) as sock:
        sock.sendall(("GET %s HTTP/1.1\r\nHost: %s\r\n"
                      "Connection: close\r\n\r\n"
                      % (path, SERVER_NAME)).encode())
        buf = b""
        while True:
            try:
                chunk = sock.recv(4096)
            except (ssl.SSLZeroReturnError, ConnectionResetError):
                break
            if not chunk:
                break
            buf += chunk

    head, _, body = buf.partition(b"\r\n\r\n")
    first = head.split(b"\r\n")[0].decode(errors="replace")
    status = int(first.split()[1]) if len(first.split()) > 1 else 0
    return status, body.decode(errors="replace")


def test_serves_a_page_over_tls(tls_server):
    _, ip = tls_server
    status, body = _get(ip, "/")
    assert status == 200, body
    assert "hello over tls" in body


def test_the_certificate_is_verified_not_merely_presented(tls_server):
    """The same request against a client that trusts the CLIENT CA instead.

    That CA is real and signs real certificates -- it just did not sign the
    board's. A client that accepts this is not verifying anything, and the
    positive test above cannot tell the difference on its own.
    """
    _, ip = tls_server
    with pytest.raises(ssl.SSLError):
        _get(ip, "/", ctx=_context(ca=CLIENT_CA))


def test_the_hostname_is_checked(tls_server):
    """Connect asking for a name the certificate does not carry."""
    _, ip = tls_server
    ctx = _context()
    raw = socket.create_connection((ip, PORT), timeout=25.0)
    with pytest.raises(ssl.CertificateError):
        ctx.wrap_socket(raw, server_hostname="not-the-board.invalid")
    raw.close()


def test_query_arguments_survive_tls(tls_server):
    """The request parser reads through Stream, not the concrete client."""
    _, ip = tls_server
    status, body = _get(ip, "/echo?v=roundtrip")
    assert status == 200, body
    assert body.strip() == "roundtrip"


def test_a_response_larger_than_one_record_arrives_whole(tls_server):
    """5000 bytes, against MBEDTLS_SSL_OUT_CONTENT_LEN of 2048.

    mbedtls fragments application data itself, but only if the caller keeps
    feeding it: mbedtls_ssl_write returns how much it took, which is at most
    one record. A write loop that treated a short write as done would truncate
    every response over 2 KB, and every response under it would be fine.
    """
    _, ip = tls_server
    status, body = _get(ip, "/big")
    assert status == 200, body
    assert len(body) == 5000, len(body)
    assert body == "0123456789" * 500


def test_the_server_sees_the_client_address(tls_server):
    """server.client() has to coerce back to EthernetClientSecure; remoteIP()
    is not on the Client interface, so this only compiles and only answers if
    the template's ClientType is threaded through."""
    _, ip = tls_server
    status, body = _get(ip, "/who")
    assert status == 200, body
    # Whatever this machine's address on that subnet is -- the point is that it
    # is an address at all rather than 0.0.0.0.
    assert body.strip().count(".") == 3, body
    assert body.strip() != "0.0.0.0"


def test_a_missing_route_is_a_404_over_tls(tls_server):
    _, ip = tls_server
    status, body = _get(ip, "/nothing-here")
    assert status == 404, body


def test_several_connections_in_a_row(tls_server):
    """A browser opens several. Each is a full handshake and a full session,
    and the failure this catches is the third one failing because the first
    two never gave their memory back."""
    _, ip = tls_server
    for i in range(5):
        status, body = _get(ip, "/")
        assert status == 200, "request %d: %s" % (i, body)


def test_the_session_memory_comes_back(tls_server):
    """An mbedtls session is tens of kilobytes. Four requests that leaked one
    each would be unmistakable here; the tolerance is for the heap's own
    fragmentation, not for a leak."""
    board, ip = tls_server
    before = int(_kv(board.command("heap"))["tls_heap"])
    for _ in range(4):
        status, _body = _get(ip, "/")
        assert status == 200
    time.sleep(0.5)
    after = int(_kv(board.command("heap"))["tls_heap"])
    assert after > before - 8192, (before, after)


def _sync_clock(board):
    """Mutual TLS, and only mutual TLS, needs the board's clock to be right.

    Serving HTTPS does not: the client checks the server's certificate against
    the CLIENT's clock. The moment the board verifies a certificate itself, a
    clock stuck before the certificate was issued rejects everything.
    """
    out = board.command("clock sync", timeout=35.0)
    if _kv(out).get("tls_rtc") != "1":
        pytest.skip("no NTP answer -- is there a route to the internet? " + out)


def test_mutual_tls_refuses_a_client_with_no_certificate(tls_server):
    board, ip = tls_server
    _sync_clock(board)
    assert "tls_mtls=1" in board.command("mtls on")
    try:
        with pytest.raises((ssl.SSLError, ConnectionResetError, OSError)):
            _get(ip, "/", timeout=10.0)
    finally:
        board.command("mtls off")


def test_mutual_tls_serves_a_client_with_the_right_certificate(tls_server):
    board, ip = tls_server
    _sync_clock(board)
    assert "tls_mtls=1" in board.command("mtls on")
    try:
        status, body = _get(ip, "/", ctx=_context(client_cert=True))
        assert status == 200, body
        assert "hello over tls" in body
    finally:
        board.command("mtls off")


def test_mutual_tls_with_a_clock_behind_says_the_clock_is_the_problem(tls_server):
    """The failure this exists to make legible.

    A board with no battery-backed clock boots in the past, and then a client
    certificate that is perfectly valid is refused as "not yet valid". The
    handshake error is X509_CERT_VERIFY_FAILED either way -- the same code as
    "signed by the wrong CA" -- so the flags and the RTC hint are the only
    thing that tells the two apart. Someone will hit this; it should not cost
    them an afternoon.
    """
    board, ip = tls_server
    assert _kv(board.command("clock behind"))["tls_rtc"] == "0"
    assert "tls_mtls=1" in board.command("mtls on")
    try:
        with pytest.raises((ssl.SSLError, ConnectionResetError, OSError)):
            _get(ip, "/", ctx=_context(client_cert=True), timeout=10.0)

        info = _kv(board.command("tlsinfo"))
        # MBEDTLS_X509_BADCERT_FUTURE
        assert int(info["tls_vferr"]) & 0x0200, info
        assert "RTC" in info["tls_vfstr"], info
    finally:
        board.command("mtls off")
        board.command("clock sync", timeout=35.0)


def test_a_clock_behind_does_not_stop_the_board_serving_https(tls_server):
    """The other half of the statement above: with mutual TLS off, the clock
    is irrelevant and the server must still work. A fix for the case above that
    started requiring a clock unconditionally would fail here."""
    board, ip = tls_server
    board.command("clock behind")
    try:
        status, body = _get(ip, "/")
        assert status == 200, body
        assert "hello over tls" in body
    finally:
        board.command("clock sync", timeout=35.0)


def test_the_server_still_answers_after_a_refused_handshake(tls_server):
    """A failed handshake must not take the listener down with it -- the
    commonest way an embedded TLS server dies is that the socket a rejected
    client left behind is never released, and the fourth one fills the pending
    queue for good."""
    _, ip = tls_server
    for _ in range(4):
        try:
            _get(ip, "/", ctx=_context(ca=CLIENT_CA), timeout=10.0)
        except (ssl.SSLError, OSError):
            pass
    status, body = _get(ip, "/")
    assert status == 200, body
