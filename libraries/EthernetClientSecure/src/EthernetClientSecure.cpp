#include "EthernetClientSecure.h"

#include <string.h>

extern "C" {
#include "ch32h4_rtc.h"
#include "mbedtls/error.h"
}

/* See the note in EthernetTlsSession.cpp: net_sockets.h is not in this build,
 * so the transport error codes are restated. */
#ifndef MBEDTLS_ERR_NET_CONNECT_FAILED
#define MBEDTLS_ERR_NET_CONNECT_FAILED  -0x0044
#endif

bool EthernetClientSecure::newSession() {
    release();
    _s = new EthernetTlsSession();
    return _s != nullptr;
}

int EthernetClientSecure::connect(const char *host, uint16_t port) {
    stop();
    if (!newSession()) {
        _last_error = MBEDTLS_ERR_SSL_ALLOC_FAILED;
        return 0;
    }
    _s->tcp.setTimeout(_timeout_ms);
    if (!_s->tcp.connect(host, port)) {
        _s->last_error = MBEDTLS_ERR_NET_CONNECT_FAILED;
        return 0;
    }
    if (!_s->handshake(false, _hostname ? _hostname : host, _ca_pem,
                       _cert_pem, _key_pem, _insecure, false,
                       _handshake_ms)) {
        _s->close();
        return 0;
    }
    _s->connected = true;
    return 1;
}

int EthernetClientSecure::connect(IPAddress ip, uint16_t port) {
    stop();
    if (!newSession()) {
        _last_error = MBEDTLS_ERR_SSL_ALLOC_FAILED;
        return 0;
    }
    _s->tcp.setTimeout(_timeout_ms);
    if (!_s->tcp.connect(ip, port)) {
        _s->last_error = MBEDTLS_ERR_NET_CONNECT_FAILED;
        return 0;
    }
    /* No hostname unless setHostname() supplied one. Connecting by address to
     * a server whose certificate names a host will fail verification, which is
     * correct -- the certificate does not say anything about that address. */
    if (!_s->handshake(false, _hostname, _ca_pem, _cert_pem, _key_pem,
                       _insecure, false, _handshake_ms)) {
        _s->close();
        return 0;
    }
    _s->connected = true;
    return 1;
}

size_t EthernetClientSecure::write(const uint8_t *buf, size_t size) {
    if (!_s || !_s->connected || size == 0) {
        return 0;
    }
    size_t sent = 0;
    const uint32_t start = millis();
    while (sent < size) {
        int ret = mbedtls_ssl_write(&_s->ssl, buf + sent, size - sent);
        if (ret > 0) {
            sent += (size_t)ret;
            continue;
        }
        if (ret != MBEDTLS_ERR_SSL_WANT_READ
            && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            _s->last_error = ret;
            _s->connected = false;
            break;
        }
        if ((millis() - start) > _timeout_ms) {
            break;
        }
        yield();
    }
    return sent;
}

int EthernetClientSecure::available() {
    if (!_s || !_s->connected) {
        return 0;
    }
    /* Ask mbedtls what it has already decrypted first; only then pump, so a
     * sketch polling available() does not spin the network stack for data that
     * is already sitting in the record buffer. */
    int n = (int)mbedtls_ssl_get_bytes_avail(&_s->ssl);
    if (n > 0) {
        return n;
    }

    /* Nothing buffered: give the transport a chance to deliver a record.
     * mbedtls_ssl_read with a zero length does the reading without consuming,
     * which is how the ESP cores do this too. */
    yield();
    int ret = mbedtls_ssl_read(&_s->ssl, nullptr, 0);
    if (ret < 0 && ret != MBEDTLS_ERR_SSL_WANT_READ
        && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
        if (ret != MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            _s->last_error = ret;
        }
        _s->connected = false;
        return 0;
    }
    return (int)mbedtls_ssl_get_bytes_avail(&_s->ssl);
}

int EthernetClientSecure::read(uint8_t *buf, size_t size) {
    if (!_s || !_s->connected || size == 0) {
        return -1;
    }
    int ret = mbedtls_ssl_read(&_s->ssl, buf, size);
    if (ret > 0) {
        return ret;
    }
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return 0;
    }
    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
        _s->connected = false;
        return -1;
    }
    _s->last_error = ret;
    _s->connected = false;
    return -1;
}

int EthernetClientSecure::read() {
    uint8_t b;
    /* Poll rather than return -1 immediately: Stream's own helpers -- and a
     * great deal of sketch code -- assume read() blocks up to the timeout. */
    const uint32_t start = millis();
    for (;;) {
        int n = read(&b, 1);
        if (n == 1) {
            return b;
        }
        if (n < 0 || (millis() - start) > _timeout_ms) {
            return -1;
        }
        yield();
    }
}

int EthernetClientSecure::peek() {
    /* mbedtls has no peek: a record is decrypted into its buffer and consumed
     * from it, and there is no way to look without taking. Reporting -1 is
     * honest; a one-byte pushback buffer here would be a second place for the
     * stream position to live. */
    return -1;
}

void EthernetClientSecure::flush() {
    if (_s) {
        _s->tcp.flush();
    }
}

void EthernetClientSecure::stop() {
    if (_s) {
        _s->close();
    }
}

uint8_t EthernetClientSecure::connected() {
    if (!_s || !_s->connected) {
        return 0;
    }
    /* Data still buffered counts as connected, the same way EthernetClient
     * does it -- otherwise the last response of every half-closed connection
     * is dropped, which is most HTTPS responses. */
    if (mbedtls_ssl_get_bytes_avail(&_s->ssl) > 0) {
        return 1;
    }
    return _s->tcp.connected() ? 1 : 0;
}

String EthernetClientSecure::verifyErrorString() const {
    const uint32_t flags = verifyError();
    if (flags == 0) {
        return String("ok");
    }
    char buf[256];
    mbedtls_x509_crt_verify_info(buf, sizeof(buf), "", flags);
    String s(buf);
    s.trim();

    /* The commonest cause on a board with no battery, and the one whose
     * message sends people looking at the wrong thing. */
    if ((flags & MBEDTLS_X509_BADCERT_FUTURE) && !ch32h4_rtc_is_set()) {
        s += " (the RTC has not been set -- every certificate looks not yet "
             "valid; sync the clock first, see NTP.h)";
    }
    return s;
}

String EthernetClientSecure::lastErrorString() const {
    const int err = lastError();
    if (err == 0) {
        return String("ok");
    }
    char buf[128];
    mbedtls_strerror(err, buf, sizeof(buf));
    return String(buf);
}
