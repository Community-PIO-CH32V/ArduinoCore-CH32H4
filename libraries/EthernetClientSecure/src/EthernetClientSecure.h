/* EthernetClientSecure -- TLS over EthernetClient, on mbedTLS.
 *
 * The shape is WiFiClientSecure's, from the ESP cores, because that is what
 * sketches are written against:
 *
 *     EthernetClientSecure client;
 *     client.setCACert(root_ca_pem);
 *     if (client.connect("example.com", 443)) { client.print("GET / ..."); }
 *
 * setInsecure() exists and is honestly named. It skips certificate
 * verification entirely, which means the connection is encrypted against a
 * passive listener and offers nothing at all against anyone who can answer for
 * the server -- which, on a network you do not control, is the threat. It is
 * there for bringing a board up, not for shipping.
 *
 * Certificate dates are checked against the RTC. A board whose clock has not
 * been set is somewhere in the year 2000, so every certificate reads as "not
 * yet valid" and every connection fails -- correctly, and confusingly. Sync
 * the clock first: see NTP.h. verifyError() says so in as many words when that
 * is what happened.
 */
#pragma once

#include <Arduino.h>
#include <Client.h>

#include "EthernetClient.h"

#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/ssl.h"
#include "mbedtls/x509_crt.h"

class EthernetClientSecure : public arduino::Client {
public:
    EthernetClientSecure() { }
    ~EthernetClientSecure() { stop(); }

    /* Trust this PEM chain, and nothing else. The pointer is kept, not
     * copied -- a string literal or a global is fine, a stack buffer is not. */
    void setCACert(const char *rootCA) { _ca_pem = rootCA; _insecure = false; }

    /* A client certificate, for servers that ask for one. */
    void setCertificate(const char *clientCert) { _cert_pem = clientCert; }
    void setPrivateKey(const char *clientKey) { _key_pem = clientKey; }

    /* Do not verify the server at all. See the header comment: this is for
     * bring-up. */
    void setInsecure() { _insecure = true; }

    /* Send this name in SNI and check it against the certificate. Defaults to
     * whatever host connect() was given, which is what anyone wants; this is
     * for connecting by address to a server with a name. */
    void setHostname(const char *name) { _hostname = name; }

    void setTimeout(unsigned long ms) {
        Stream::setTimeout(ms);
        _timeout_ms = (uint32_t)ms;
    }
    void setHandshakeTimeout(uint32_t ms) { _handshake_ms = ms; }

    int connect(IPAddress ip, uint16_t port) override;
    int connect(const char *host, uint16_t port) override;

    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t *buf, size_t size) override;
    using Print::write;

    int available() override;
    int read() override;
    int read(uint8_t *buf, size_t size) override;
    int peek() override;
    void flush() override;
    void stop() override;
    uint8_t connected() override;
    operator bool() override { return connected() != 0; }

    /* 0 when the certificate was accepted, otherwise mbedtls's verification
     * flags. verifyErrorString() renders them, including the clock case. */
    uint32_t verifyError() const { return _verify_flags; }
    String verifyErrorString() const;

    /* The last mbedtls return code, negative. Distinct from verifyError():
     * a handshake can fail for reasons that have nothing to do with the
     * certificate, and reporting those as a verification failure sends people
     * looking in the wrong place. */
    int lastError() const { return _last_error; }
    String lastErrorString() const;

    IPAddress remoteIP() const { return _tcp.remoteIP(); }
    uint16_t remotePort() const { return _tcp.remotePort(); }

private:
    bool handshake(const char *hostname);
    void freeContexts();

    static int bioSend(void *ctx, const unsigned char *buf, size_t len);
    static int bioRecv(void *ctx, unsigned char *buf, size_t len);

    EthernetClient _tcp;

    mbedtls_ssl_context _ssl = {};
    mbedtls_ssl_config _conf = {};
    mbedtls_ctr_drbg_context _drbg = {};
    mbedtls_entropy_context _entropy = {};
    mbedtls_x509_crt _ca = {};
    mbedtls_x509_crt _cert = {};
    mbedtls_pk_context _key = {};

    const char *_ca_pem = nullptr;
    const char *_cert_pem = nullptr;
    const char *_key_pem = nullptr;
    const char *_hostname = nullptr;

    bool _insecure = false;
    bool _connected = false;
    bool _contexts_up = false;
    uint32_t _verify_flags = 0;
    int _last_error = 0;
    uint32_t _timeout_ms = 5000;
    uint32_t _handshake_ms = 20000;
    uint32_t _deadline = 0;
};
