"""The same MP3 over the network, decoding to the same bytes.

The whole file turns on one equality: the board fetching tone.mp3 over HTTP
must produce the PCM checksum it produces decoding tone.mp3 out of flash. That
proves the network path changes nothing about the audio, and it needs no
internet -- the server is this machine.
"""
import datetime
import http.server
import ipaddress
import pathlib
import socket
import ssl
import threading
import time

import pytest

cryptography = pytest.importorskip("cryptography")
from cryptography import x509                                     # noqa: E402
from cryptography.hazmat.primitives import hashes, serialization  # noqa: E402
from cryptography.hazmat.primitives.asymmetric import rsa         # noqa: E402
from cryptography.x509.oid import NameOID                         # noqa: E402

from conftest import kv

DATA = pathlib.Path(__file__).resolve().parents[1] / "data" / "tone.mp3"
MP3 = DATA.read_bytes()


class _Handler(http.server.BaseHTTPRequestHandler):
    slow = False
    base = ""          # set by the fixture; playlists must name absolute URLs
    tls_base = ""      # set by the TLS fixture, for the cross-scheme redirect

    def do_GET(self):
        # Redirects. The same-scheme one is followed inside HTTPClient and must
        # cost no hop; the cross-scheme one it refuses, so RadioStream has to
        # take it as a hop or a station that moved to TLS stops playing.
        if self.path == "/moved-here":
            self.send_response(302)
            self.send_header("Location", "%s/tone.mp3" % self.base)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return
        if self.path == "/moved-to-tls":
            self.send_response(302)
            self.send_header("Location", "%s/tone.mp3" % self.tls_base)
            self.send_header("Content-Length", "0")
            self.end_headers()
            return

        # Shoutcast metadata, interleaved the way a real station does it: a
        # length byte plus that many 16-byte units of text after every metaint
        # bytes of audio. Sent ONLY to a request carrying Icy-MetaData: 1, so
        # this doubles as proof the client asks -- a server that is not asked
        # sends no metaint, and the title stays empty forever.
        if self.path == "/icy.mp3":
            title = b"StreamTitle='Test Title';"
            pad = (-len(title)) % 16
            block = bytes([(len(title) + pad) // 16]) + title + b"\0" * pad
            if self.headers.get("Icy-MetaData", "") != "1":
                body, metaint = MP3, 0
            else:
                metaint = 4096
                out = bytearray()
                for i in range(0, len(MP3), metaint):
                    chunk = MP3[i:i + metaint]
                    out += chunk
                    # Only after a FULL metaint of audio; a short tail at the
                    # end of the file is followed by nothing.
                    if len(chunk) == metaint:
                        out += block
                body = bytes(out)
            self.send_response(200)
            self.send_header("Content-Type", "audio/mpeg")
            if metaint:
                self.send_header("icy-metaint", str(metaint))
                self.send_header("icy-name", "test")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        # Playlists, so the resolve path is exercised over a real socket and
        # not only against synthetic bodies in the parser tests.
        if self.path.endswith(".m3u"):
            body = ("#EXTM3U\n#EXTINF:-1,Test\n%s/tone.mp3\n" % self.base).encode()
            self.send_response(200)
            self.send_header("Content-Type", "audio/x-mpegurl")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return
        if self.path.endswith(".pls"):
            # Points at the .m3u, so this is a two-hop chain.
            body = ("[playlist]\nnumberofentries=1\nFile1=%s/list.m3u\n"
                    % self.base).encode()
            self.send_response(200)
            self.send_header("Content-Type", "audio/x-scpls")
            self.send_header("Content-Length", str(len(body)))
            self.end_headers()
            self.wfile.write(body)
            return

        self.send_response(200)
        self.send_header("Content-Type", "audio/mpeg")
        self.send_header("Content-Length", str(len(MP3)))
        self.end_headers()
        if not self.slow:
            self.wfile.write(MP3)
            return
        # Below real time: 2 s of audio dribbled out over about 6 s.
        step = max(1, len(MP3) // 60)
        for i in range(0, len(MP3), step):
            self.wfile.write(MP3[i:i + step])
            self.wfile.flush()
            time.sleep(0.1)

    def log_message(self, *args):
        pass


@pytest.fixture(scope="module")
def radio(mp3_board):
    ip = kv(mp3_board.banner).get("net_ip", "")
    if not ip or ip == "0.0.0.0":
        pytest.skip("the board has no DHCP lease -- is the RJ45 plugged in?")
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.connect((ip, 1234))
    host_ip = probe.getsockname()[0]
    probe.close()
    srv = http.server.ThreadingHTTPServer((host_ip, 0), _Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    base = "http://%s:%d" % (host_ip, srv.server_address[1])
    _Handler.base = base
    yield mp3_board, base
    srv.shutdown()
    srv.server_close()


def test_the_network_path_decodes_to_the_same_bytes(radio):
    """THE test. Anything that corrupts a byte in transit, drops a chunk, or
    mishandles a partial read shows up as a different checksum."""
    board, base = radio
    _Handler.slow = False
    local = kv(board.command("decode", timeout=30))["pcm_fnv"]
    r = kv(board.command("netplay " + base + "/tone.mp3", timeout=60))
    assert r["http_rc"] == 200, r.raw
    assert r["decode_errors"] == 0, r.raw
    assert r["pcm_fnv"] == local, (
        "the stream decoded differently from the same file in flash")


def test_a_slow_server_still_delivers_every_byte(radio):
    """A stall must degrade, not stop.

    Delivered below real time, the stream should still finish with every frame
    intact. Silence is recoverable; a radio that gives up is not.
    """
    board, base = radio
    _Handler.slow = True
    try:
        local = kv(board.command("decode", timeout=30))["pcm_fnv"]
        r = kv(board.command("netplay " + base + "/tone.mp3", timeout=90))
        assert r["http_rc"] == 200, r.raw
        assert r["pcm_fnv"] == local, "a slow stream lost or altered data"
        assert r["decode_errors"] == 0, r.raw
    finally:
        _Handler.slow = False


# ---- the same thing over TLS ------------------------------------------------

def _pem(obj, key=False):
    if key:
        return obj.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.TraditionalOpenSSL,
            serialization.NoEncryption()).decode()
    return obj.public_bytes(serialization.Encoding.PEM).decode()


def _make_ca():
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, "mp3 test ca")])
    now = datetime.datetime.now(datetime.timezone.utc)
    cert = (x509.CertificateBuilder()
            .subject_name(subject).issuer_name(subject)
            .public_key(key.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(now - datetime.timedelta(days=1))
            .not_valid_after(now + datetime.timedelta(days=30))
            .add_extension(x509.BasicConstraints(ca=True, path_length=None), True)
            .sign(key, hashes.SHA256()))
    return key, cert


def _make_server_cert(ca_key, ca_cert, ip):
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, ip)])
    now = datetime.datetime.now(datetime.timezone.utc)
    cert = (x509.CertificateBuilder()
            .subject_name(subject).issuer_name(ca_cert.subject)
            .public_key(key.public_key())
            .serial_number(x509.random_serial_number())
            .not_valid_before(now - datetime.timedelta(days=1))
            .not_valid_after(now + datetime.timedelta(days=30))
            .add_extension(x509.BasicConstraints(ca=False, path_length=None), True)
            # An IP SAN, because the board connects by address. mbedtls checks
            # the name it was given, so a DNS-only cert would fail for the
            # right reason and the wrong test.
            .add_extension(x509.SubjectAlternativeName(
                [x509.IPAddress(ipaddress.ip_address(ip))]), False)
            .sign(ca_key, hashes.SHA256()))
    return key, cert


@pytest.fixture(scope="module")
def tls_radio(radio, tmp_path_factory):
    board, base = radio
    host_ip = base.split("//")[1].split(":")[0]
    ca_key, ca_cert = _make_ca()
    srv_key, srv_cert = _make_server_cert(ca_key, ca_cert, host_ip)

    d = tmp_path_factory.mktemp("mp3tls")
    crt, keyf = d / "s.crt", d / "s.key"
    crt.write_text(_pem(srv_cert))
    keyf.write_text(_pem(srv_key, key=True))

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(str(crt), str(keyf))
    srv = http.server.ThreadingHTTPServer((host_ip, 0), _Handler)
    srv.socket = ctx.wrap_socket(srv.socket, server_side=True)
    threading.Thread(target=srv.serve_forever, daemon=True).start()

    # The clock first: an unset one reads as the year 2000 and mbedtls then
    # rejects every certificate as not yet valid. See docs/hazards.md.
    board.command("rtcset %d" % int(time.time()), timeout=10)
    board.command("cabegin", timeout=10)
    for line in _pem(ca_cert).strip().splitlines():
        board.command("caline " + line, timeout=10)
    board.command("caend", timeout=10)

    tls_base = "https://%s:%d" % (host_ip, srv.server_address[1])
    _Handler.tls_base = tls_base
    yield board, tls_base
    srv.shutdown()
    srv.server_close()


def test_https_decodes_to_the_same_bytes(tls_radio):
    """TLS must not change the audio either.

    Verified against the uploaded CA rather than with setInsecure(), because a
    real station needs verification and because this is also what proves the
    clock got set.
    """
    board, base = tls_radio
    _Handler.slow = False
    local = kv(board.command("decode", timeout=30))["pcm_fnv"]
    r = kv(board.command("netplays " + base + "/tone.mp3", timeout=120))
    assert r["http_rc"] == 200, r.raw
    assert r["decode_errors"] == 0, r.raw
    assert r["pcm_fnv"] == local, "the TLS path altered the audio"


@pytest.mark.parametrize("link,hops", [("tone.mp3", 0), ("list.m3u", 1),
                                       ("list.pls", 2)])
def test_a_playlist_link_resolves_to_the_same_audio(radio, link, hops):
    """A station link, resolved over a real socket, must reach the same audio.

    Three shapes: the stream itself, a one-hop .m3u, and a .pls that points at
    the .m3u -- a two-hop chain, which is what real stations do. All three must
    produce the checksum the board gets decoding the file from flash, because
    resolution must not alter what is played.

    The parser is tested separately against synthetic bodies. This is the loop
    around it: content-type sniffing, reading the body, and re-connecting to
    the next hop.
    """
    board, base = radio
    _Handler.slow = False
    local = kv(board.command("decode", timeout=30))["pcm_fnv"]
    r = kv(board.command("radio %s/%s" % (base, link), timeout=90))
    assert r["radio_ok"] == 1, r.raw
    assert r["hops"] == hops, r.raw
    assert r["final_url"] == "%s/tone.mp3" % base, r.raw
    assert r["decode_errors"] == 0, r.raw
    assert r["pcm_fnv"] == local, "resolution changed the audio"


def test_a_same_scheme_redirect_costs_no_hop(radio):
    """HTTPClient follows this one itself, and must keep doing so.

    Asserting hops == 0 is the point: if RadioStream ever started handling
    redirects it does not need to, every station with a load balancer would
    burn its hop budget on them and long playlist chains would stop resolving.
    """
    board, base = radio
    _Handler.slow = False
    local = kv(board.command("decode", timeout=30))["pcm_fnv"]
    r = kv(board.command("radio %s/moved-here" % base, timeout=90))
    assert r["radio_ok"] == 1, r.raw
    assert r["hops"] == 0, r.raw
    assert r["pcm_fnv"] == local, "a redirect changed the audio"


def test_an_http_link_that_moved_to_https_still_plays(tls_radio):
    """The published link is http, the audio is behind TLS.

    HTTPClient refuses this redirect, because the client object it already
    holds cannot speak TLS and connecting it to port 443 would send the request
    in the clear. So the 3xx surfaces to RadioStream, which must rebuild the
    client for the new scheme and carry on. Stations that moved to TLS while
    leaving their old .m3u published are the reason this matters.

    Only tls_radio is requested even though the redirect starts on the plain
    server: tls_radio depends on radio, so both are up, and asking for both
    here trips a double-finalizer assertion inside pytest.
    """
    board, _ = tls_radio
    base = _Handler.base
    _Handler.slow = False
    local = kv(board.command("decode", timeout=30))["pcm_fnv"]
    r = kv(board.command("radio %s/moved-to-tls" % base, timeout=120))
    assert r["radio_ok"] == 1, r.raw
    assert r["hops"] == 1, r.raw
    assert r["final_url"].startswith("https://"), r.raw
    assert r["decode_errors"] == 0, r.raw
    assert r["pcm_fnv"] == local, "crossing to TLS mid-chain changed the audio"


def test_shoutcast_metadata_is_asked_for_and_stripped(radio):
    """Titles arrive, and not one byte of them reaches the decoder.

    Two failures hide behind each other here. Never sending Icy-MetaData: 1
    gets clean audio and no titles, which looks like broken title parsing. And
    asking without stripping puts a text block into the audio every few
    seconds, which sounds like a broken decoder. So both halves are asserted
    against the one checksum: the server interleaves metadata only for a client
    that asks, and the PCM must still match the same file decoded from flash.
    """
    board, base = radio
    _Handler.slow = False
    local = kv(board.command("decode", timeout=30))["pcm_fnv"]
    r = kv(board.command("radio %s/icy.mp3" % base, timeout=90))
    assert r["radio_ok"] == 1, r.raw
    assert r["metaint"] == 4096, (
        "no metaint -- the request went out without Icy-MetaData: 1")
    assert r["title_changes"] >= 1, "the title block was never parsed"
    assert r["decode_errors"] == 0, r.raw
    assert r["pcm_fnv"] == local, "metadata leaked into the audio"


def test_one_client_serves_both_schemes_in_either_order(tls_radio):
    """The trap that used to be in HTTPClient, now a regression test.

    Configuring TLS once built the secure client there and then, and
    begin(url) only made a client when it had none -- so whichever scheme came
    first decided what every later URL got. An http:// URL on a TLS client
    opened a plain socket to port 80 and started a handshake with a server
    speaking HTTP; the reverse left a plain client pointed at port 443. Either
    way it failed as a connection error, said nothing about certificates, and
    worked when the schemes happened to agree.

    Both orders are exercised because the fix has two directions and only one
    of them was reachable through the tests that already existed. The sketch
    holds a single RadioStream, so these four commands share one
    HTTPClientSecure across four scheme changes.
    """
    board, tls_base = tls_radio
    plain_base = _Handler.base
    _Handler.slow = False
    local = kv(board.command("decode", timeout=30))["pcm_fnv"]

    for label, url in (("https first", tls_base + "/tone.mp3"),
                       ("then http", plain_base + "/tone.mp3"),
                       ("https again", tls_base + "/tone.mp3"),
                       ("http again", plain_base + "/tone.mp3")):
        r = kv(board.command("radio " + url, timeout=120))
        assert r["radio_ok"] == 1, "%s: %s" % (label, r.raw)
        assert r["decode_errors"] == 0, "%s: %s" % (label, r.raw)
        assert r["pcm_fnv"] == local, "%s: the audio differed" % label
