/* EthernetClient -- a TCP connection, in the shape Arduino sketches expect.
 *
 * Copying one shares the connection rather than duplicating it. That is not a
 * convenience: `EthernetClient c = server.accept();` and passing a client to a
 * helper by value are both ordinary, and either would close the socket twice
 * if the copy owned it.
 */
#pragma once

#include <Arduino.h>
#include <Client.h>

#include "LwipClientContext.h"

class EthernetClient : public arduino::Client {
public:
    EthernetClient() { }
    explicit EthernetClient(LwipClientContext *ctx) : _ctx(ctx) {
        if (_ctx) {
            _ctx->ref();
        }
    }
    EthernetClient(const EthernetClient &other) : _ctx(other._ctx) {
        if (_ctx) {
            _ctx->ref();
        }
    }
    EthernetClient &operator=(const EthernetClient &other) {
        if (this != &other) {
            if (other._ctx) {
                other._ctx->ref();
            }
            if (_ctx) {
                _ctx->unref();
            }
            _ctx = other._ctx;
        }
        return *this;
    }
    ~EthernetClient() {   /* not override: arduino::Client has no virtual dtor */
        if (_ctx) {
            _ctx->unref();
        }
    }

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

    /* Beyond the Client interface, and worth having: a sketch that cannot see
     * who connected cannot log or filter anything. */
    IPAddress remoteIP() const { return _ctx ? _ctx->remoteIP() : IPAddress(0, 0, 0, 0); }
    uint16_t remotePort() const { return _ctx ? _ctx->remotePort() : 0; }
    IPAddress localIP() const { return _ctx ? _ctx->localIP() : IPAddress(0, 0, 0, 0); }
    uint16_t localPort() const { return _ctx ? _ctx->localPort() : 0; }

    /* Nagle off. Matters for request/response protocols, where the algorithm
     * holds a small reply back waiting for data that is not coming. */
    void setNoDelay(bool on) { if (_ctx) { _ctx->setNoDelay(on); } }
    bool getNoDelay() const { return _ctx && _ctx->getNoDelay(); }

    void setTimeout(unsigned long ms) {
        Stream::setTimeout(ms);
        if (_ctx) {
            _ctx->setTimeout((uint32_t)ms);
        }
    }

    /* Two clients are the same client when they share a connection. Sketches
     * keep a list of connected clients and compare against it. */
    bool operator==(const EthernetClient &rhs) const { return _ctx == rhs._ctx; }
    bool operator!=(const EthernetClient &rhs) const { return _ctx != rhs._ctx; }

private:
    LwipClientContext *_ctx = nullptr;
};
