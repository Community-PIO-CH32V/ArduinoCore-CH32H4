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

    def do_GET(self):
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

    yield board, "https://%s:%d" % (host_ip, srv.server_address[1])
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
