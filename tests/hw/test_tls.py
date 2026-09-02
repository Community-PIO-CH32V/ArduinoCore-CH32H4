"""mbedTLS: the ECDC AES accelerator, the TRNG as entropy, and a TLS client.

The AES known-answer tests matter most and are cheapest. The ECDC block's ECB
and CTR mode selectors are swapped relative to the vendor header
(openwch/ch32h417 issue #10), and choosing wrong is silent: CTR with a constant
counter encrypts, decrypts, round-trips, and protects nothing. An all-zero
plaintext is the one input where the two modes agree, which is exactly why the
vectors here use a non-zero one.

The TLS tests run against a server started by this file with a certificate
signed by a CA generated here. A public HTTPS host would prove a handshake
works and nothing about whether verification does -- a client that accepts
everything passes that test. With our own CA we can also present a certificate
signed by a DIFFERENT one, and a clock set to the wrong decade, and check that
both are refused.
"""
import datetime
import ipaddress
import socket
import ssl
import tempfile
import threading
import time
from pathlib import Path

import pytest

cryptography = pytest.importorskip("cryptography",
                                   reason="needed to generate test certificates")
from cryptography import x509                                    # noqa: E402
from cryptography.hazmat.primitives import hashes, serialization  # noqa: E402
from cryptography.hazmat.primitives.asymmetric import rsa         # noqa: E402
from cryptography.x509.oid import NameOID                         # noqa: E402

PORT = 8443


def _kv(out):
    d = {}
    for line in out.splitlines():
        line = line.strip()
        if "=" in line:
            k, _, v = line.partition("=")
            d[k.strip()] = v.strip()
    return d


# ---- certificates -----------------------------------------------------------

def _make_ca(name):
    key = rsa.generate_private_key(public_exponent=65537, key_size=2048)
    subject = x509.Name([x509.NameAttribute(NameOID.COMMON_NAME, name)])
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
            # the name it was given against the certificate, so a cert with only
            # a DNS name would fail here for the right reason and the wrong
            # test.
            .add_extension(x509.SubjectAlternativeName(
                [x509.IPAddress(ipaddress.ip_address(ip))]), False)
            .sign(ca_key, hashes.SHA256()))
    return key, cert


def _pem(obj, key=False):
    if key:
        return obj.private_bytes(
            serialization.Encoding.PEM,
            serialization.PrivateFormat.TraditionalOpenSSL,
            serialization.NoEncryption()).decode()
    return obj.public_bytes(serialization.Encoding.PEM).decode()


# ---- fixtures ---------------------------------------------------------------

@pytest.fixture(scope="module")
def tls(tls_board):
    """A board with an address, a CA, and a TLS server on this machine."""
    banner = tls_board.banner
    ip = _kv(banner).get("ip", "")
    if not ip or ip == "0.0.0.0":
        pytest.skip("the board has no DHCP lease -- is the RJ45 plugged in?")

    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.connect((ip, 1234))
    host_ip = probe.getsockname()[0]
    probe.close()

    ca_key, ca_cert = _make_ca("ch32h4 test CA")
    _, wrong_cert = _make_ca("ch32h4 WRONG CA")
    srv_key, srv_cert = _make_server_cert(ca_key, ca_cert, host_ip)

    tmp = Path(tempfile.mkdtemp())
    (tmp / "srv.pem").write_text(_pem(srv_cert) + _pem(srv_key, key=True))

    ctx = ssl.SSLContext(ssl.PROTOCOL_TLS_SERVER)
    ctx.load_cert_chain(str(tmp / "srv.pem"))
    state = {"stop": False}

    def serve():
        srv = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        srv.setsockopt(socket.SOL_SOCKET, socket.SO_REUSEADDR, 1)
        srv.bind(("0.0.0.0", PORT))
        srv.listen(5)
        srv.settimeout(1.0)
        while not state["stop"]:
            try:
                conn, _ = srv.accept()
            except socket.timeout:
                continue
            except OSError:
                break
            try:
                t = ctx.wrap_socket(conn, server_side=True)
                t.settimeout(5)
                try:
                    t.recv(4096)
                except Exception:
                    pass
                t.sendall(b"HTTP/1.0 200 OK\r\nContent-Length: 2\r\n\r\nhi")
                t.close()
            except Exception:
                # A refused handshake is the expected outcome of the negative
                # tests, not an error here.
                try:
                    conn.close()
                except Exception:
                    pass
        srv.close()

    th = threading.Thread(target=serve, daemon=True)
    th.start()
    time.sleep(0.5)

    def send_ca(pem_text):
        tls_board.command("cabegin", timeout=5.0)
        for line in pem_text.strip().splitlines():
            tls_board.command("caline " + line, timeout=5.0)
        return _kv(tls_board.command("caend", timeout=5.0))

    ctxobj = {"b": tls_board, "board_ip": ip, "host_ip": host_ip,
              "ca": _pem(ca_cert), "wrong_ca": _pem(wrong_cert),
              "send_ca": send_ca}
    yield ctxobj
    state["stop"] = True


def _set_clock(board):
    board.command(f"rtcset {int(time.time())}", timeout=10.0)


# ---- AES --------------------------------------------------------------------

def test_aes_known_answers(tls_board):
    """FIPS-197 appendix C, all three key lengths, encrypt and decrypt.

    The 192- and 256-bit vectors are not redundant with the 128-bit one: the
    key sits right-aligned in a 256-bit register file, and a per-size placement
    that got 128 right left longer keys assembled differently from the one
    asked for. AES-128 passed while every AES-256 vector failed.
    """
    d = _kv(tls_board.command("aestest", timeout=20.0))
    assert d["aes_vectors_passed"] == "3", d
    for bits in (128, 192, 256):
        assert d[f"aes{bits}_enc"] == "ok", d
        assert d[f"aes{bits}_dec"] == "ok", d


def test_aes_is_ecb_and_not_ctr(tls_board):
    """The mode selector, checked by a value that can only be ECB.

    aes_zero_block is E(key, 0), which ECB and zero-counter CTR both produce --
    it is reported for reference. The proof is the FIPS vectors above, whose
    plaintext is non-zero: under CTR with a constant keystream the ciphertext
    would be plaintext XOR E(0), which is not the published answer.

    This test states the relationship so that a future SDK drop swapping the
    header back has something to fail against.
    """
    d = _kv(tls_board.command("aestest", timeout=20.0))
    assert d["aes_zero_block"] == "c6a13b37878f5b826f4f8162a1c8d879", (
        "E(key, 0) for the FIPS-197 AES-128 key changed -- the block cipher "
        "mode or the key placement is not what it was", d)
    assert d["aes128_enc"] == "ok", (
        "the non-zero-plaintext vector fails, which is what a CTR/ECB mix-up "
        "looks like", d)


def test_aes_cbc_and_ctr_round_trip(tls_board):
    """CBC and CTR are built on the hardware ECB block in software.

    Both check that the ciphertext differs from the plaintext as well as that
    it round-trips -- a cipher that returned its input would pass a round-trip
    test perfectly.
    """
    d = _kv(tls_board.command("aestest", timeout=20.0))
    assert d["aes_cbc"] == "ok", d
    assert d["aes_ctr"] == "ok", d


def test_the_drbg_seeds_from_the_hardware(tls_board):
    """mbedtls's CTR_DRBG, seeded through mbedtls_hardware_poll().

    MBEDTLS_NO_PLATFORM_ENTROPY is set, so this is the only entropy source
    configured: a seed failure here means TLS has no randomness at all.
    """
    d = _kv(tls_board.command("drbgtest", timeout=20.0))
    assert d["drbg_seed_rc"] == "0", ("the DRBG could not be seeded -- the "
                                      "TRNG is the only entropy source", d)
    assert d["drbg_rc"] == "0", d
    assert d["drbg_differ"] == "1", ("two draws came back identical", d)
    assert int(d["drbg_a"], 16) != 0, d


# ---- TLS --------------------------------------------------------------------

def test_handshake_with_a_trusted_ca(tls):
    """A full TLS handshake with verification, and an HTTP response over it."""
    _set_clock(tls["b"])
    assert tls["send_ca"](tls["ca"])["ca_bytes"] != "0"
    out = tls["b"].command(
        f"tlsverify {tls['host_ip']} {PORT}", timeout=60.0)
    d = _kv(out)
    assert d["tls_connect"] == "1", out
    assert d["tls_verify"] == "0x0", out
    assert "200 OK" in d.get("tls_response", ""), out
    assert int(d["tls_bytes"]) > 0, out
    # available() has to agree with read(), or a sketch written the ordinary
    # Arduino way stalls while one written backwards works.
    assert int(d["tls_avail_seen"]) > 0, (
        "read() returned data that available() never reported", out)


def test_a_certificate_from_another_ca_is_refused(tls):
    """The test that distinguishes verification from encryption.

    A client that accepts every certificate passes every positive test.
    """
    _set_clock(tls["b"])
    tls["send_ca"](tls["wrong_ca"])
    out = tls["b"].command(f"tlsverify {tls['host_ip']} {PORT}", timeout=60.0)
    d = _kv(out)
    assert d["tls_connect"] == "0", ("a certificate from an untrusted CA was "
                                     "accepted", out)
    assert d["tls_verify"] != "0x0", out
    assert "not correctly signed" in d.get("tls_verify_str", ""), out


def test_an_unset_clock_refuses_every_certificate(tls):
    """And says so in a way that names the actual cause.

    A board with no battery starts in the year 2000, so every certificate is
    "not yet valid" -- a real failure whose message otherwise sends people
    looking at their CA.
    """
    tls["send_ca"](tls["ca"])
    tls["b"].command("rtcset 946684800", timeout=10.0)   # 2000-01-01
    try:
        out = tls["b"].command(
            f"tlsverify {tls['host_ip']} {PORT}", timeout=60.0)
        d = _kv(out)
        assert d["tls_connect"] == "0", ("a certificate was accepted with the "
                                         "clock in the year 2000", out)
        assert "future" in d.get("tls_verify_str", "").lower(), out
        assert "RTC has not been set" in d.get("tls_verify_str", ""), (
            "the error does not mention the clock, which is the actual "
            "cause", out)
    finally:
        _set_clock(tls["b"])


def test_insecure_mode_connects_without_a_ca(tls):
    """setInsecure() has to work, and be the only thing that does.

    Without it and without a CA, connect() refuses rather than silently
    accepting anything -- which is checked by the untrusted-CA test above.
    """
    out = tls["b"].command(f"tlsinsecure {tls['host_ip']} {PORT}", timeout=60.0)
    d = _kv(out)
    assert d["tls_connect"] == "1", out
    assert "200 OK" in d.get("tls_response", ""), out


def test_it_recovers_after_a_refused_connection(tls):
    """The contexts have to be freed and rebuilt cleanly.

    Three failed handshakes have run by now, each allocating an entropy
    context, a DRBG, a parsed CA chain and an SSL context. A leak or a
    half-freed context shows up as the next connection failing for no reason.
    """
    _set_clock(tls["b"])
    tls["send_ca"](tls["ca"])
    for _ in range(3):
        out = tls["b"].command(
            f"tlsverify {tls['host_ip']} {PORT}", timeout=60.0)
        d = _kv(out)
        assert d["tls_connect"] == "1", out
        assert d["tls_verify"] == "0x0", out


def test_tls_did_not_leak(tls):
    """Handshakes come and go; the heap must come back."""
    def free():
        d = _kv(tls["b"].command("tlsinfo", timeout=10.0))
        return int(d["heap_free"])

    _set_clock(tls["b"])
    tls["send_ca"](tls["ca"])
    before = free()
    for _ in range(4):
        tls["b"].command(f"tlsverify {tls['host_ip']} {PORT}", timeout=60.0)
    after = free()
    assert before - after < 4096, (
        f"{before - after} bytes went missing over 4 TLS connections")
