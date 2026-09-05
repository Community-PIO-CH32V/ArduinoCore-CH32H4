"""WebServer and HTTPClient, on hardware, over plain HTTP.

Both libraries came from arduino-pico with WiFiClient replaced by the
arduino::Client interface they were already restricted to. That substitution is
the only systematic change, and it is exactly the kind that compiles cleanly
and then fails somewhere in the middle of a request -- a virtual that was not
virtual, a concrete type the template needed and no longer has. So this drives
both directions:

  * the PC as the client, against WebServer on the board -- a real HTTP
    implementation on the other end, which is the only thing that says the
    responses are well formed rather than merely self-consistent;
  * the board as the client, HTTPClient fetching from a server this file
    stands up. NOT from the board's own WebServer, which cannot work: lwIP is
    built without LWIP_NETIF_LOOPBACK, so a packet to the board's own address
    leaves by the Ethernet port and never comes back -- and even with loopback
    the sketch is single-threaded, so the request would wait for a
    handleClient() that the blocked loop() will never reach.

There is a companion for HTTPS in test_tls_server.py. The two images are
separate on purpose: the same ported code, once with mbedtls under it and once
without, so a break that mbedtls happens to hide still shows up here.
"""
import http.client
import http.server
import socket
import threading

import pytest

PORT = 80


def _kv(out):
    d = {}
    for line in out.splitlines():
        line = line.strip()
        if "=" in line:
            k, _, v = line.partition("=")
            d[k.strip()] = v.strip()
    return d


@pytest.fixture(scope="module")
def web(web_board):
    banner = web_board.banner
    ip = _kv(banner).get("web_ip", "")
    if not ip or ip == "0.0.0.0":
        pytest.skip("the board has no DHCP lease -- is the RJ45 plugged in?")
    return web_board, ip


def _get(ip, path, timeout=10.0):
    """One request per connection. http.client rather than a hand-rolled
    socket, so the response has to satisfy a parser nobody here wrote."""
    conn = http.client.HTTPConnection(ip, PORT, timeout=timeout)
    try:
        conn.request("GET", path)
        r = conn.getresponse()
        return r.status, r.read().decode(errors="replace"), dict(r.getheaders())
    finally:
        conn.close()


# ---- the board as server ----------------------------------------------------

def test_serves_a_page(web):
    _, ip = web
    status, body, _headers = _get(ip, "/")
    assert status == 200, body
    assert "hello from ch32h4" in body


def test_the_response_declares_its_length(web):
    """A response with neither Content-Length nor chunked encoding "works"
    against a client that reads to EOF and hangs against one that does not.
    http.client is the second kind, which is why this is checkable at all."""
    _, ip = web
    status, body, headers = _get(ip, "/")
    assert status == 200
    lower = {k.lower(): v for k, v in headers.items()}
    assert "content-length" in lower or \
        lower.get("transfer-encoding", "").lower() == "chunked", headers
    if "content-length" in lower:
        assert int(lower["content-length"]) == len(body)


def test_query_arguments_are_parsed(web):
    _, ip = web
    status, body, _h = _get(ip, "/echo?v=hello%20there")
    assert status == 200, body
    assert body.strip() == "hello there", repr(body)


def test_an_unknown_route_is_404(web):
    _, ip = web
    status, body, _h = _get(ip, "/no-such-thing")
    assert status == 404, body


def test_several_requests_in_a_row(web):
    """Each on its own connection, which is what a browser does after the
    first. The failure mode is the second request hanging because the first
    connection was never released."""
    _, ip = web
    for i in range(8):
        status, body, _h = _get(ip, "/")
        assert status == 200, "request %d: %s" % (i, body)
        assert "hello from ch32h4" in body


def test_the_hit_counter_agrees_with_what_we_sent(web):
    """Cheap, and it catches a whole class of nonsense: a server answering
    from a cache, or a proxy on the network answering for it."""
    board, ip = web
    before = int(_kv(board.command("webinfo"))["web_hits"])
    for _ in range(3):
        assert _get(ip, "/")[0] == 200
    after = int(_kv(board.command("webinfo"))["web_hits"])
    assert after - before == 3, (before, after)


def test_a_client_that_connects_and_says_nothing_is_dropped(web):
    """HTTP_MAX_DATA_WAIT exists so one silent connection cannot hold the
    server. Open one, leave it, and check the server still answers -- with
    MAX_PENDING of 4 a leak here would take the listener down."""
    board, ip = web
    dead = [socket.create_connection((ip, PORT), timeout=5) for _ in range(3)]
    try:
        status, body, _h = _get(ip, "/", timeout=20.0)
        assert status == 200, body
    finally:
        for s in dead:
            s.close()
    # And still, after they go away.
    assert _get(ip, "/", timeout=20.0)[0] == 200


# ---- the board as client ----------------------------------------------------

class _Handler(http.server.BaseHTTPRequestHandler):
    """Fixed answers, plus one that is deliberately awkward."""

    def do_GET(self):                      # noqa: N802  (the API's spelling)
        if self.path == "/hello":
            body = b"hello from the test host"
        elif self.path == "/empty":
            body = b""
        elif self.path == "/big":
            # Larger than one TCP segment and than HTTPClient's read buffer,
            # so the client has to loop rather than read once and stop.
            body = b"0123456789" * 900
        elif self.path.startswith("/echo?"):
            body = self.path.split("?", 1)[1].encode()
        elif self.path == "/missing":
            self.send_response(404)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        else:
            self.send_response(400)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        self.send_response(200)
        self.send_header("Content-Type", "text/plain")
        self.send_header("Content-Length", str(len(body)))
        self.end_headers()
        self.wfile.write(body)

    def log_message(self, *args):
        pass


@pytest.fixture(scope="module")
def host_http(web):
    """An HTTP server on this machine, on the interface the board can see."""
    _board, board_ip = web
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.connect((board_ip, 1234))
    host_ip = probe.getsockname()[0]
    probe.close()

    srv = http.server.ThreadingHTTPServer((host_ip, 0), _Handler)
    t = threading.Thread(target=srv.serve_forever, daemon=True)
    t.start()
    base = "http://%s:%d" % (host_ip, srv.server_address[1])
    yield base
    srv.shutdown()
    srv.server_close()


def test_httpclient_fetches_a_page(web, host_http):
    board, _ip = web
    out = board.command("webget " + host_http + "/hello", timeout=25.0)
    d = _kv(out)
    assert d.get("web_get_rc") == "200", out
    assert d.get("web_get_body") == "hello from the test host", out


def test_httpclient_reports_a_404_as_a_404(web, host_http):
    """Not as an error. HTTPClient's return code is the HTTP status when there
    was a response and a negative number when there was not, and collapsing
    the two is the classic port bug."""
    board, _ip = web
    d = _kv(board.command("webget " + host_http + "/missing", timeout=25.0))
    assert d.get("web_get_rc") == "404", d


def test_httpclient_gets_the_body_length_right(web, host_http):
    board, _ip = web
    out = board.command("webget " + host_http + "/echo?v=abcdefgh",
                        timeout=25.0)
    d = _kv(out)
    assert d.get("web_get_rc") == "200", out
    assert d.get("web_get_len") == "10", out
    assert d.get("web_get_body") == "v=abcdefgh", out


def test_httpclient_reads_a_body_larger_than_its_buffer(web, host_http):
    """9000 bytes: more than one segment and more than one read. A client that
    stopped at the first chunk would return a short body and no error."""
    board, _ip = web
    d = _kv(board.command("webget " + host_http + "/big", timeout=30.0))
    assert d.get("web_get_rc") == "200", d
    assert d.get("web_get_len") == "9000", d


def test_httpclient_handles_an_empty_body(web, host_http):
    """Content-Length: 0 is a valid response and the one most likely to be
    read as "the connection failed"."""
    board, _ip = web
    d = _kv(board.command("webget " + host_http + "/empty", timeout=25.0))
    assert d.get("web_get_rc") == "200", d
    assert d.get("web_get_len") == "0", d


def test_httpclient_reports_a_refused_connection(web):
    """Port 1 on the board's own gateway-facing side: nothing listens. The
    return has to be negative rather than a status code, or every failed
    request looks like a successful one to a caller checking `rc > 0`."""
    board, _ip = web
    d = _kv(board.command("webget http://127.0.0.1:1/nothing", timeout=25.0))
    assert d.get("web_get_rc") is not None
    assert int(d["web_get_rc"]) < 0, d


def test_the_server_still_works_after_the_client_ran(web, host_http):
    """Both libraries in one image, sharing one lwIP. The failure this catches
    is HTTPClient's socket teardown taking the listener with it."""
    board, ip = web
    assert _kv(board.command("webget " + host_http + "/hello",
                             timeout=25.0)).get("web_get_rc") == "200"
    status, body, _h = _get(ip, "/")
    assert status == 200, body
    assert "hello from ch32h4" in body
