/* A TLS server: EthernetServer with a handshake on every accepted socket.
 *
 * This is what WebServerSecure is built on. It is the same shape as
 * EthernetServer -- begin(), accept(), available(), close() -- so anything
 * templated over a server type takes it unchanged.
 *
 * YOU MUST SUPPLY A CERTIFICATE AND A KEY. A server has nothing to present
 * without them and cannot complete a handshake; setCertificate() and
 * setPrivateKey() before begin() are not optional the way they are on the
 * client, and accept() reports the failure rather than returning a client
 * that looks connected.
 *
 * THE HANDSHAKE IN accept() BLOCKS. It runs to completion -- calling yield(),
 * so the network stack keeps running and other connections are still served
 * at the lwIP level -- but the sketch does not return from accept() until the
 * handshake finishes or times out. On a LAN that is a few tens of
 * milliseconds; the timeout bounds the bad case. It is stated here because a
 * server that must answer other requests during a handshake needs a design
 * this class does not provide, and finding that out from a stall is worse
 * than reading it.
 *
 * ONE HANDSHAKE IS EXPENSIVE. Each live connection holds an mbedTLS context,
 * its record buffers and the parsed certificate -- on the order of 20 KB with
 * the default buffer sizes, against 255 KB of RAM. A handful of concurrent
 * TLS connections is the realistic budget on this part, not dozens.
 */
#pragma once

#include <Arduino.h>
#include <Server.h>

#include "EthernetClientSecure.h"
#include "EthernetServer.h"

#if !defined(MBEDTLS_SSL_SRV_C)
#error "EthernetServerSecure needs the server half of mbedTLS, which is a build option: set board_build.tls = mbedtls-server. The default, board_build.tls = mbedtls, is the client only -- without MBEDTLS_SSL_SRV_C mbedtls has no server handshake at all, and this would otherwise fail at link time with an unhelpful message about mbedtls_ssl_handshake_server_step."
#endif

class EthernetServerSecure : public arduino::Server {
public:
    /* What WebServerTemplate asks a server for. */
    using ClientType = EthernetClientSecure;

    explicit EthernetServerSecure(uint16_t port = 443) : _tcp(port) { }
    EthernetServerSecure(IPAddress addr, uint16_t port)
        : _tcp(addr, port) { }

    /* The server's own certificate chain and key, in PEM. Pointers are kept,
     * not copied: a string literal or a global is fine, a stack buffer is
     * not. Both are required. */
    void setCertificate(const char *serverCert) { _cert_pem = serverCert; }
    void setPrivateKey(const char *serverKey) { _key_pem = serverKey; }

    /* Ask every client for a certificate and refuse those that cannot show
     * one signed by this CA -- mutual TLS. Off by default, because a browser
     * has no such certificate and would be refused. */
    void setClientCACert(const char *clientCA) { _client_ca_pem = clientCA; }
    void requireClientCert(bool on) { _require_client_cert = on; }

    void setHandshakeTimeout(uint32_t ms) { _handshake_ms = ms; }

    /* Nagle off on every accepted connection. WebServerTemplate::begin() sets
     * this, and it matters more here than on plain TCP: a handshake is a
     * sequence of small records that each wait for the peer's answer, so
     * holding one back for 40 ms happens several times per connection. */
    void setNoDelay(bool on) { _tcp.setNoDelay(on); }
    bool getNoDelay() const { return _tcp.getNoDelay(); }

    void begin() override { _tcp.begin(); }
    void begin(uint16_t port) { _tcp.begin(port); }
    void end() { _tcp.end(); }
    void close() { _tcp.end(); }
    void stop() { _tcp.end(); }

    /* A connection with its handshake done, or a disconnected client.
     *
     * A failed handshake is not reported through the return value beyond the
     * client being unusable -- there is nowhere to put an error in the
     * Arduino shape -- but lastHandshakeError() holds the mbedtls code, which
     * is the difference between "a browser hung up on a self-signed
     * certificate" and "the key does not match the certificate". */
    EthernetClientSecure accept();
    EthernetClientSecure available() { return accept(); }

    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t *, size_t) override {
        /* EthernetServer broadcasts to every connected client. There is no
         * equivalent here: each TLS connection has its own keys and record
         * sequence, so "the same bytes to everyone" would mean encrypting
         * separately per connection -- which is what writing to each client
         * already is. Returning 0 rather than silently doing nothing that
         * looks like something. */
        return 0;
    }
    using Print::write;

    operator bool() const { return (bool)_tcp; }
    uint16_t port() const { return _tcp.port(); }

    /* Forwarded so this can stand in for EthernetServer under WebServer. */
    bool hasClientData() const { return _tcp.hasClientData(); }
    bool hasMaxPendingClients() const { return _tcp.hasMaxPendingClients(); }

    /* The mbedtls code from the last handshake attempt, or 0. */
    int lastHandshakeError() const { return _last_error; }
    String lastHandshakeErrorString() const;

    /* Why the last CLIENT certificate was refused: mbedtls's verification
     * flags, or 0. Only meaningful with requireClientCert(true).
     *
     * Worth having separately from lastHandshakeError(), which reports
     * X509_CERT_VERIFY_FAILED for every one of these -- and the difference
     * between "signed by the wrong CA" and "the board thinks it is the year
     * 2000, so the certificate is not valid yet" is the whole diagnosis.
     * The second is the commonest, on a part with no battery-backed clock:
     * sync the RTC before turning mutual TLS on. See NTP.h. */
    uint32_t lastVerifyError() const { return _verify_flags; }
    String lastVerifyErrorString() const;

private:
    EthernetServer _tcp;
    const char *_cert_pem = nullptr;
    const char *_key_pem = nullptr;
    const char *_client_ca_pem = nullptr;
    bool _require_client_cert = false;
    uint32_t _handshake_ms = 10000;
    int _last_error = 0;
    uint32_t _verify_flags = 0;
};
