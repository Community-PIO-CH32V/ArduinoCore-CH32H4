#include "EthernetTlsSession.h"

#include <string.h>

extern "C" {
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
#ifndef MBEDTLS_ERR_NET_CONN_RESET
#define MBEDTLS_ERR_NET_CONN_RESET      -0x0050
#endif

EthernetTlsSession::EthernetTlsSession() { }

EthernetTlsSession::~EthernetTlsSession() {
    close();
}

/* The BIO. mbedtls does not know what a socket is; these two functions are the
 * whole of what it needs.
 *
 * Both are non-blocking, and return WANT_READ/WANT_WRITE rather than zero when
 * there is nothing to do. Returning zero means "connection closed" to mbedtls,
 * so a BIO that reported an empty buffer that way would tear down every
 * connection the moment it paused. */
int EthernetTlsSession::bioSend(void *ctx, const unsigned char *buf,
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

int EthernetTlsSession::bioRecv(void *ctx, unsigned char *buf, size_t len) {
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

void EthernetTlsSession::freeContexts() {
    if (!_up) {
        return;
    }
    mbedtls_ssl_free(&ssl);
    mbedtls_ssl_config_free(&conf);
    mbedtls_ctr_drbg_free(&drbg);
    mbedtls_entropy_free(&entropy);
    mbedtls_x509_crt_free(&ca);
    mbedtls_x509_crt_free(&cert);
    mbedtls_pk_free(&key);
    _up = false;
}

void EthernetTlsSession::close() {
    if (connected) {
        /* close_notify, so the peer sees an orderly shutdown rather than a
         * truncation it is entitled to treat as an attack. One attempt: if it
         * cannot go out, the connection is already gone. */
        mbedtls_ssl_close_notify(&ssl);
        connected = false;
    }
    tcp.stop();
    freeContexts();
}

bool EthernetTlsSession::handshake(bool server, const char *hostname,
                                   const char *ca_pem, const char *cert_pem,
                                   const char *key_pem, bool insecure,
                                   bool require_client_cert,
                                   uint32_t timeout_ms) {
    verify_flags = 0;
    last_error = 0;

#if !defined(MBEDTLS_SSL_SRV_C)
    if (server) {
        /* Should be unreachable: EthernetServerSecure.h refuses to compile
         * without the server build. Here so that a direct caller gets an
         * error code rather than a handshake that cannot terminate. */
        last_error = MBEDTLS_ERR_SSL_FEATURE_UNAVAILABLE;
        return false;
    }
#endif

    mbedtls_ssl_init(&ssl);
    mbedtls_ssl_config_init(&conf);
    mbedtls_ctr_drbg_init(&drbg);
    mbedtls_entropy_init(&entropy);
    mbedtls_x509_crt_init(&ca);
    mbedtls_x509_crt_init(&cert);
    mbedtls_pk_init(&key);
    _up = true;

    /* Seed the DRBG. mbedtls_hardware_poll() in the port layer is the only
     * entropy source configured, and it is the conditioned TRNG. */
    static const char pers[] = "ch32h4-tls";
    last_error = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &entropy,
                                       (const unsigned char *)pers,
                                       sizeof(pers) - 1);
    if (last_error != 0) {
        return false;
    }

    last_error = mbedtls_ssl_config_defaults(
        &conf,
        server ? MBEDTLS_SSL_IS_SERVER : MBEDTLS_SSL_IS_CLIENT,
        MBEDTLS_SSL_TRANSPORT_STREAM, MBEDTLS_SSL_PRESET_DEFAULT);
    if (last_error != 0) {
        return false;
    }
    mbedtls_ssl_conf_rng(&conf, mbedtls_ctr_drbg_random, &drbg);

    /* Who verifies whom.
     *
     * A client checks the server, and must be told what to trust. A server
     * does NOT check the client by default -- that is what browsers expect,
     * and demanding a certificate no browser has would refuse every
     * connection -- but will if a CA is configured and asked to. */
    if (server) {
        if (ca_pem && require_client_cert) {
            last_error = mbedtls_x509_crt_parse(
                &ca, (const unsigned char *)ca_pem, strlen(ca_pem) + 1);
            if (last_error != 0) {
                return false;
            }
            mbedtls_ssl_conf_ca_chain(&conf, &ca, nullptr);
            mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
        } else {
            mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
        }
    } else if (insecure) {
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_NONE);
    } else if (ca_pem) {
        /* +1 on the length: mbedtls requires PEM input to include its
         * terminating NUL in the count, and silently fails to parse without
         * it -- which presents as "no CA configured" much later. */
        last_error = mbedtls_x509_crt_parse(
            &ca, (const unsigned char *)ca_pem, strlen(ca_pem) + 1);
        if (last_error != 0) {
            return false;
        }
        mbedtls_ssl_conf_ca_chain(&conf, &ca, nullptr);
        mbedtls_ssl_conf_authmode(&conf, MBEDTLS_SSL_VERIFY_REQUIRED);
    } else {
        /* No CA and not explicitly insecure. Refusing is the only safe
         * reading: a connection that silently accepted anything here would
         * look exactly like a working one. */
        last_error = MBEDTLS_ERR_SSL_CA_CHAIN_REQUIRED;
        return false;
    }

    /* The endpoint's own certificate. Optional for a client -- only servers
     * that ask for one need it -- and mandatory for a server, which has
     * nothing to present without it and would fail mid-handshake with an
     * error naming the cipher suite rather than the missing certificate. */
    if (cert_pem && key_pem) {
        last_error = mbedtls_x509_crt_parse(
            &cert, (const unsigned char *)cert_pem, strlen(cert_pem) + 1);
        if (last_error == 0) {
            last_error = mbedtls_pk_parse_key(
                &key, (const unsigned char *)key_pem, strlen(key_pem) + 1,
                nullptr, 0, mbedtls_ctr_drbg_random, &drbg);
        }
        if (last_error == 0) {
            last_error = mbedtls_ssl_conf_own_cert(&conf, &cert, &key);
        }
        if (last_error != 0) {
            return false;
        }
    } else if (server) {
        last_error = MBEDTLS_ERR_SSL_PRIVATE_KEY_REQUIRED;
        return false;
    }

    last_error = mbedtls_ssl_setup(&ssl, &conf);
    if (last_error != 0) {
        return false;
    }

    if (!server && hostname && hostname[0]) {
        /* SNI, and what the certificate's names are checked against. Without
         * it a server hosting several sites answers with the wrong one, and
         * verification fails for a reason that looks like a bad CA.
         *
         * Client only: on a server this field means the name to REQUIRE of a
         * client certificate, which is not what a caller passing a hostname
         * means by it. */
        last_error = mbedtls_ssl_set_hostname(&ssl, hostname);
        if (last_error != 0) {
            return false;
        }
    }

    mbedtls_ssl_set_bio(&ssl, &tcp, bioSend, bioRecv, nullptr);

    const uint32_t start = millis();
    for (;;) {
        int ret = mbedtls_ssl_handshake(&ssl);
        if (ret == 0) {
            break;
        }
        if (ret != MBEDTLS_ERR_SSL_WANT_READ
            && ret != MBEDTLS_ERR_SSL_WANT_WRITE) {
            last_error = ret;
            verify_flags = mbedtls_ssl_get_verify_result(&ssl);
            return false;
        }
        if ((millis() - start) > timeout_ms) {
            last_error = MBEDTLS_ERR_SSL_TIMEOUT;
            return false;
        }
        /* The peer's records arrive through Ethernet::update(), which runs
         * from yield(). A handshake loop without this cannot complete. */
        yield();
    }

    verify_flags = mbedtls_ssl_get_verify_result(&ssl);
    /* A server with VERIFY_NONE gets no flags to check, and a client that
     * asked for none said so on purpose. */
    if (!server && !insecure && verify_flags != 0) {
        last_error = MBEDTLS_ERR_X509_CERT_VERIFY_FAILED;
        return false;
    }
    return true;
}
