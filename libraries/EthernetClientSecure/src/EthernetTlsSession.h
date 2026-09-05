/* The mbedTLS state behind a secure connection, on the heap and refcounted.
 *
 * WHY THIS IS NOT JUST MEMBERS OF EthernetClientSecure, which is where it
 * started: an mbedtls_ssl_context holds a pointer to the mbedtls_ssl_config
 * it was set up with, and the BIO holds a pointer to the transport. If those
 * live as sibling members of a class, then copying OR moving that class
 * leaves the new object's ssl context pointing at the OLD object's config and
 * socket -- which keeps working right up until the original is destroyed, and
 * then reads freed memory. There is no way to write a correct copy
 * constructor for that layout; the pointers would have to be re-seated, and
 * mbedtls offers no API to do it.
 *
 * So the whole bundle lives in one heap allocation that nothing moves, and
 * copying a client copies a pointer and bumps a count -- exactly how
 * EthernetClient and its LwipClientContext already work. That is what makes
 * `EthernetClientSecure c = server.accept();` safe, and WebServer needs it:
 * it does `new ClientType(_server.accept())` on every connection.
 */
#pragma once

#include <Arduino.h>

#include "EthernetClient.h"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/pk.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

class EthernetTlsSession {
public:
    EthernetTlsSession();
    ~EthernetTlsSession();

    void ref() { _refs++; }
    void unref() {
        if (--_refs <= 0) {
            delete this;
        }
    }

    /* Bring up the mbedtls contexts and run a handshake to completion.
     *
     * `server` picks the endpoint. The two differ in more than a constant:
     * a client must have a CA or be explicitly insecure and sends SNI, while
     * a server must have its own certificate and by default asks the client
     * for nothing. Both are enforced in the implementation rather than left
     * to the caller.
     */
    bool handshake(bool server, const char *hostname,
                   const char *ca_pem, const char *cert_pem,
                   const char *key_pem, bool insecure,
                   bool require_client_cert, uint32_t timeout_ms);

    void close();

    EthernetClient tcp;
    bool connected = false;
    uint32_t verify_flags = 0;
    int last_error = 0;

private:
    void freeContexts();

    static int bioSend(void *ctx, const unsigned char *buf, size_t len);
    static int bioRecv(void *ctx, unsigned char *buf, size_t len);

    int _refs = 1;
    bool _up = false;

public:
    /* Public because EthernetClientSecure drives read/write through them
     * directly; there is no value in a second layer of forwarding. */
    mbedtls_ssl_context ssl = {};
    mbedtls_ssl_config conf = {};
    mbedtls_ctr_drbg_context drbg = {};
    mbedtls_entropy_context entropy = {};
    mbedtls_x509_crt ca = {};
    mbedtls_x509_crt cert = {};
    mbedtls_pk_context key = {};
};
