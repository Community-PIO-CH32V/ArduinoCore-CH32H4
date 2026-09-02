#include "EthernetClientSecure.h"

#include <string.h>

extern "C" {
#include "ch32h4_rtc.h"
#include "mbedtls/error.h"
}

/* The transport error codes mbedtls uses.
 *
 * They live in mbedtls/net_sockets.h, which is the BSD-socket transport --
 * disabled here, because there are no BSD sockets on this board and the header
 * declares a pile of functions that would need them. The codes themselves are
 * just numbers, and mbedtls_strerror() still renders them, so they are
 * restated rather than dragging the header in. Guarded, so nothing breaks if a
 * sketch does include it. */
#ifndef MBEDTLS_ERR_NET_CONNECT_FAILED
#define MBEDTLS_ERR_NET_CONNECT_FAILED  -0x0044
#endif
#ifndef MBEDTLS_ERR_NET_CONN_RESET
#define MBEDTLS_ERR_NET_CONN_RESET      -0x0050
#endif

/* The BIO. mbedtls does not know what a socket is; these two functions are the
 * whole of what it needs.
 *
 * Both are non-blocking, and return WANT_READ/WANT_WRITE rather than zero when
 * there is nothing to do. Returning zero means "connection closed" to mbedtls,
 * so a BIO that reported an empty buffer that way would tear down every
 * connection the moment it paused. */
int EthernetClientSecure::bioSend(void *ctx, const unsigned char *buf,
                                  size_t len) {
    EthernetClient *tcp = static_cast<EthernetClient *>(ctx);
    if (!tcp->connected()) {
        return MBEDTLS_ERR_NET_CONN_RESET;
    }
    size_t n = tcp->write(buf, len);
    if (n == 0) {
        return MBEDTLS_ERR_SSL_WANT_WRITE;
    }
    return (int)n;
}

int EthernetClientSecure::bioRecv(void *ctx, unsigned char *buf, size_t len) {
    EthernetClient *tcp = static_cast<EthernetClient *>(ctx);
    int have = tcp->available();
    if (have <= 0) {
        if (!tcp->connected()) {
            return MBEDTLS_ERR_NET_CONN_RESET;
        }
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    if ((size_t)have > len) {
        have = (int)len;
    }
    int n = tcp->read(buf, (size_t)have);
    if (n <= 0) {
        return MBEDTLS_ERR_SSL_WANT_READ;
    }
    return n;
}

void EthernetClientSecure::freeContexts() {
    if (!_contexts_up) {
        return;
    }
    mbedtls_ssl_free(&_ssl);
    mbedtls_ssl_config_free(&_conf);
    mbedtls_ctr_drbg_free(&_drbg);
    mbedtls_entropy_free(&_entropy);
    mbedtls_x509_crt_free(&_ca);
    mbedtls_x509_crt_free(&_cert);
    mbedtls_pk_free(&_key);
    _contexts_up = false;
}

bool EthernetClientSecure::handshake(const char *hostname) {
    _verify_flags = 0;
    _last_error = 0;

    mbedtls_ssl_init(&_ssl);
    mbedtls_ssl_config_init(&_conf);
    mbedtls_ctr_drbg_init(&_drbg);
    mbedtls_entropy_init(&_entropy);
    mbedtls_x509_crt_init(&_ca);
    mbedtls_x509_crt_init(&_cert);
    mbedtls_pk_init(&_key);
    _contexts_up = true;

    /* Seed the DRBG. mbedtls_hardware_poll() in the port layer is the only
     * entropy source configured, and it is the conditioned TRNG. */
    static const char pers[] = "ch32h4-tls";
    _last_error = mbedtls_ctr_drbg_seed(&_drbg, mbedtls_entropy_func, &_entropy,
                                        (const unsigned char *)pers,
                                        sizeof(pers) - 1);
    if (_last_error != 0) {
        return false;
    }

    _last_error = mbedtls_ssl_config_defaults(&_conf, MBEDTLS_SSL_IS_CLIENT,
                                              MBEDTLS_SSL_TRANSPORT_STREAM,
                                              MBEDTLS_SSL_PRESET_DEFAULT);
    if (_last_error != 0) {
        return false;
    }
    mbedtls_ssl_conf_rng(&_conf, mbedtls_ctr_drbg_random, &_drbg);

    if (_insecure) {
        mbedtls_ssl_conf_authmode(&_conf, MBEDTLS_SSL_VERIFY_NONE);
    } else if (_ca_pem) {
        /* +1 on the length: mbedtls requires PEM input to include its
         * terminating NUL in the count, and silently fails to parse without
         * it -- which presents as "no CA configured" much later. */
        _last_error = mbedtls_x509_crt_parse(
            &_ca, (const unsigned char *)_ca_pem, strlen(_ca_pem) + 1);
        if (_last_error != 0) {
            return false;
        }
        mbedtls_ssl_conf_ca_chain(&_conf, &_ca, nullptr);
        mbedtls_ssl_conf_authmode(&_conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    } else {
        /* No CA and not explicitly insecure. Refusing is the only safe
         * reading: a connection that silently accepted anything here would
         * look exactly like a working one. */
        _last_error = MBEDTLS_ERR_SSL_CA_CHAIN_REQUIRED;
        return false;
    }

    if (_cert_pem && _key_pem) {
        _last_error = mbedtls_x509_crt_parse(
            &_cert, (const unsigned char *)_cert_pem, strlen(_cert_pem) + 1);
        if (_last_error == 0) {
            _last_error = mbedtls_pk_parse_key(
                &_key, (const unsigned char *)_key_pem, strlen(_key_pem) + 1,
                nullptr, 0, mbedtls_ctr_drbg_random, &_drbg);
        }
        if (_last_error == 0) {
            _last_error = mbedtls_ssl_conf_own_cert(&_conf, &_cert, &_key);
        }
        if (_last_error != 0) {
            return false;
        }
    }

    _last_error = mbedtls_ssl_setup(&_ssl, &_conf);
    if (_last_error != 0) {
        return false;
    }

    if (hostname && hostname[0]) {
        /* SNI, and what the certificate's names are checked against. Without
         * it a server hosting several sites answers with the wrong one, and
         * verification fails for a reason that looks like a bad CA. */
        _last_error = mbedtls_ssl_set_hostname(&_ssl, hostname);
        if (_last_error != 0) {
            return false;
        }
    }

    mbedtls_ssl_set_bio(&_ssl, &_tcp, bioSend, bioRecv, nullptr);

    const uint32_t start = millis();
    for (;;) {
        int ret = mbedtls_ssl_handshake(&_ssl);
        if (ret == 0) {
            break;
        }
        if (ret != MBEDTLS_ERR_SSL_WANT_READ
            && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            _last_error = ret;
            _verify_flags = mbedtls_ssl_get_verify_result(&_ssl);
            return false;
        }
        if ((millis() - start) > _handshake_ms) {
            _last_error = MBEDTLS_ERR_SSL_TIMEOUT;
            return false;
        }
        /* The peer's records arrive through Ethernet::update(), which runs
         * from yield(). A handshake loop without this cannot complete. */
        yield();
    }

    _verify_flags = mbedtls_ssl_get_verify_result(&_ssl);
    if (!_insecure && _verify_flags != 0) {
        _last_error = MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
        return false;
    }
    return true;
}

int EthernetClientSecure::connect(const char *host, uint16_t port) {
    stop();
    _tcp.setTimeout(_timeout_ms);
    if (!_tcp.connect(host, port)) {
        _last_error = MBEDTLS_ERR_NET_CONNECT_FAILED;
        return 0;
    }
    if (!handshake(_hostname ? _hostname : host)) {
        _tcp.stop();
        freeContexts();
        return 0;
    }
    _connected = true;
    return 1;
}

int EthernetClientSecure::connect(IPAddress ip, uint16_t port) {
    stop();
    _tcp.setTimeout(_timeout_ms);
    if (!_tcp.connect(ip, port)) {
        _last_error = MBEDTLS_ERR_NET_CONNECT_FAILED;
        return 0;
    }
    /* No hostname unless setHostname() supplied one. Connecting by address to
     * a server whose certificate names a host will fail verification, which is
     * correct -- the certificate does not say anything about that address. */
    if (!handshake(_hostname)) {
        _tcp.stop();
        freeContexts();
        return 0;
    }
    _connected = true;
    return 1;
}

size_t EthernetClientSecure::write(const uint8_t *buf, size_t size) {
    if (!_connected || size == 0) {
        return 0;
    }
    size_t sent = 0;
    const uint32_t start = millis();
    while (sent < size) {
        int ret = mbedtls_ssl_write(&_ssl, buf + sent, size - sent);
        if (ret > 0) {
            sent += (size_t)ret;
            continue;
        }
        if (ret != MBEDTLS_ERR_SSL_WANT_READ
            && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            _last_error = ret;
            _connected = false;
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
    if (!_connected) {
        return 0;
    }
    /* Ask mbedtls what it has already decrypted first; only then pump, so a
     * sketch polling available() does not spin the network stack for data that
     * is already sitting in the record buffer. */
    int n = (int)mbedtls_ssl_get_bytes_avail(&_ssl);
    if (n > 0) {
        return n;
    }

    /* Nothing buffered: give the transport a chance to deliver a record.
     * mbedtls_ssl_read with a zero length does the reading without consuming,
     * which is how the ESP cores do this too. */
    yield();
    int ret = mbedtls_ssl_read(&_ssl, nullptr, 0);
    if (ret < 0 && ret != MBEDTLS_ERR_SSL_WANT_READ
        && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
        if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
            _connected = false;
        } else {
            _last_error = ret;
            _connected = false;
        }
        return 0;
    }
    return (int)mbedtls_ssl_get_bytes_avail(&_ssl);
}

int EthernetClientSecure::read(uint8_t *buf, size_t size) {
    if (!_connected || size == 0) {
        return -1;
    }
    int ret = mbedtls_ssl_read(&_ssl, buf, size);
    if (ret > 0) {
        return ret;
    }
    if (ret == MBEDTLS_ERR_SSL_WANT_READ || ret == MBEDTLS_ERR_SSL_WANT_WRITE) {
        return 0;
    }
    if (ret == MBEDTLS_ERR_SSL_PEER_CLOSE_NOTIFY) {
        _connected = false;
        return -1;
    }
    _last_error = ret;
    _connected = false;
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
    _tcp.flush();
}

void EthernetClientSecure::stop() {
    if (_connected) {
        /* close_notify, so the peer sees an orderly shutdown rather than a
         * truncation it is entitled to treat as an attack. One attempt: if it
         * cannot go out, the connection is already gone. */
        mbedtls_ssl_close_notify(&_ssl);
        _connected = false;
    }
    _tcp.stop();
    freeContexts();
}

uint8_t EthernetClientSecure::connected() {
    if (!_connected) {
        return 0;
    }
    /* Data still buffered counts as connected, the same way EthernetClient
     * does it -- otherwise the last response of every half-closed connection
     * is dropped, which is most HTTPS responses. */
    if (mbedtls_ssl_get_bytes_avail(&_ssl) > 0) {
        return 1;
    }
    return _tcp.connected() ? 1 : 0;
}

String EthernetClientSecure::verifyErrorString() const {
    if (_verify_flags == 0) {
        return String("ok");
    }
    char buf[256];
    mbedtls_x509_crt_verify_info(buf, sizeof(buf), "", _verify_flags);
    String s(buf);
    s.trim();

    /* The commonest cause on a board with no battery, and the one whose
     * message sends people looking at the wrong thing. */
    if ((_verify_flags & MBEDTLS_X509_BADCERT_FUTURE) && !ch32h4_rtc_is_set()) {
        s += " (the RTC has not been set -- every certificate looks not yet "
             "valid; sync the clock first, see NTP.h)";
    }
    return s;
}

String EthernetClientSecure::lastErrorString() const {
    if (_last_error == 0) {
        return String("ok");
    }
    char buf[128];
    mbedtls_strerror(_last_error, buf, sizeof(buf));
    return String(buf);
}
