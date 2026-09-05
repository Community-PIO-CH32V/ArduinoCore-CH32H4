/* TLS over Ethernet, in the shape of WiFiClientSecure.
 *
 * A Client that speaks TLS 1.2 and 1.3 through mbedTLS, with AES on the ECDC
 * accelerator, entropy from the hardware TRNG, and certificate validity
 * checked against the RTC -- which means a board whose clock has never been
 * set rejects every certificate as "not yet valid". That is correct, and it
 * is the commonest first failure; verifyErrorString() says so when it is what
 * happened.
 *
 * COPYING ONE IS SAFE and shares the connection, the way EthernetClient and
 * every other Arduino client behave. The mbedTLS state lives in one heap
 * allocation that nothing moves -- see EthernetTlsSession.h for why it cannot
 * live in this object -- and copies refer to it. That is what lets
 * `EthernetClientSecure c = secureServer.accept();` work, and WebServer
 * requires it.
 */
#pragma once

#include <Arduino.h>
#include <Client.h>

#include "EthernetClient.h"
#include "EthernetTlsSession.h"

class EthernetClientSecure : public arduino::Client {
public:
    EthernetClientSecure() { }
    ~EthernetClientSecure() { release(); }

    EthernetClientSecure(const EthernetClientSecure &other) { copyFrom(other); }
    EthernetClientSecure &operator=(const EthernetClientSecure &other) {
        if (this != &other) {
            release();
            copyFrom(other);
        }
        return *this;
    }

    /* Adopt an already-connected session. EthernetServerSecure builds a
     * client this way after it has run the server-side handshake. */
    explicit EthernetClientSecure(EthernetTlsSession *session) : _s(session) {
        if (_s) {
            _s->ref();
        }
    }

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
    uint32_t verifyError() const { return _s ? _s->verify_flags : 0; }
    String verifyErrorString() const;

    /* The last mbedtls return code, negative. Distinct from verifyError():
     * a handshake can fail for reasons that have nothing to do with the
     * certificate, and reporting those as a verification failure sends people
     * looking in the wrong place. */
    int lastError() const { return _s ? _s->last_error : _last_error; }
    String lastErrorString() const;

    IPAddress remoteIP() const {
        return _s ? _s->tcp.remoteIP() : IPAddress((uint32_t)0);
    }
    uint16_t remotePort() const { return _s ? _s->tcp.remotePort() : 0; }

private:
    void release() {
        if (_s) {
            _s->unref();
            _s = nullptr;
        }
    }

    void copyFrom(const EthernetClientSecure &other) {
        _s = other._s;
        if (_s) {
            _s->ref();
        }
        _ca_pem = other._ca_pem;
        _cert_pem = other._cert_pem;
        _key_pem = other._key_pem;
        _hostname = other._hostname;
        _insecure = other._insecure;
        _timeout_ms = other._timeout_ms;
        _handshake_ms = other._handshake_ms;
        _last_error = other._last_error;
    }

    /* Fresh session for a new outgoing connection. */
    bool newSession();

    EthernetTlsSession *_s = nullptr;

    const char *_ca_pem = nullptr;
    const char *_cert_pem = nullptr;
    const char *_key_pem = nullptr;
    const char *_hostname = nullptr;
    bool _insecure = false;
    uint32_t _timeout_ms = 5000;
    uint32_t _handshake_ms = 10000;

    /* Only for failures that happen before a session exists -- a TCP connect
     * that never completed. Once there is a session, its own code wins. */
    int _last_error = 0;
};
