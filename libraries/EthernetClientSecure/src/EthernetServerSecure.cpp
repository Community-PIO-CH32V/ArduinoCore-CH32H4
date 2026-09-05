/* THE WHOLE FILE IS CONDITIONAL, and the guard has to come before the include.
 *
 * Both PlatformIO's dependency finder and arduino-cli compile every source in
 * a library a sketch used, not only the ones it included. So a sketch that
 * only wants EthernetClientSecure -- the TLS client -- still gets this file
 * compiled, and without the server build that means the #error in the header
 * fires for a sketch that never asked for a server.
 *
 * Guarding on CH32H4_TLS_SERVER rather than MBEDTLS_SSL_SRV_C is deliberate:
 * the latter is only defined once the mbedtls config has been included, and
 * including it is exactly what must not happen here. The two say the same
 * thing -- CH32H4_TLS_SERVER is what makes MBEDTLS_SSL_SRV_C survive the
 * config header.
 *
 * A sketch that DOES include EthernetServerSecure.h without the option still
 * gets the header's #error, which is the case worth reporting.
 */
#if defined(CH32H4_TLS_SERVER)

#include "EthernetServerSecure.h"

extern "C" {
#include "ch32h4_rtc.h"
#include "mbedtls/error.h"
}

EthernetClientSecure EthernetServerSecure::accept() {
    /* Nothing waiting is the common case -- handleClient() calls this every
     * loop -- so it must be cheap and must not allocate. */
    EthernetClient raw = _tcp.accept();
    if (!raw.connected()) {
        return EthernetClientSecure();
    }

    if (!_cert_pem || !_key_pem) {
        /* No certificate, no handshake. Dropping the socket rather than
         * leaving it open: a client waiting on a ServerHello that will never
         * come holds a pending slot until it times out, and with four of them
         * a misconfigured server stops accepting entirely. */
        _last_error = MBEDTLS_ERR_SSL_PRIVATE_KEY_REQUIRED;
        raw.stop();
        return EthernetClientSecure();
    }

    EthernetTlsSession *s = new EthernetTlsSession();
    if (!s) {
        _last_error = MBEDTLS_ERR_SSL_ALLOC_FAILED;
        raw.stop();
        return EthernetClientSecure();
    }
    /* Shares the socket rather than duplicating it; `raw` going out of scope
     * below drops its reference and nothing else. */
    s->tcp = raw;
    s->tcp.setNoDelay(true);

    if (!s->handshake(true, nullptr, _client_ca_pem, _cert_pem, _key_pem,
                      false, _require_client_cert, _handshake_ms)) {
        _last_error = s->last_error;
        _verify_flags = s->verify_flags;
        s->close();
        s->unref();
        return EthernetClientSecure();
    }

    s->connected = true;
    _last_error = 0;
    _verify_flags = 0;

    /* The client takes its own reference; ours goes away with the local. */
    EthernetClientSecure client(s);
    s->unref();
    return client;
}

String EthernetServerSecure::lastHandshakeErrorString() const {
    if (_last_error == 0) {
        return String("ok");
    }
    char buf[128];
    mbedtls_strerror(_last_error, buf, sizeof(buf));
    return String(buf);
}

String EthernetServerSecure::lastVerifyErrorString() const {
    if (_verify_flags == 0) {
        return String("ok");
    }
    char buf[256];
    mbedtls_x509_crt_verify_info(buf, sizeof(buf), "", _verify_flags);
    String s(buf);
    s.trim();

    /* The one that is almost never what it looks like. A client certificate
     * issued today reads as "not yet valid" to a board that booted thinking it
     * is the year 2000, and nothing in the rendered flags mentions the clock. */
    if ((_verify_flags & MBEDTLS_X509_BADCERT_FUTURE) && !ch32h4_rtc_is_set()) {
        s += " (the RTC has not been set -- every certificate looks not yet "
             "valid; sync the clock first, see NTP.h)";
    }
    return s;
}

#endif  /* CH32H4_TLS_SERVER */
