/* EthernetServer -- a listening TCP port.
 *
 * Accepted connections are queued rather than handed straight to the sketch,
 * because lwIP's accept callback fires from the receive path and the sketch is
 * not there to take it. The queue is small and bounded: a server with nowhere
 * to put a connection should refuse it, not grow until the heap is gone.
 */
#pragma once

#include <Arduino.h>
#include <Server.h>

#include "EthernetClient.h"

extern "C" {
#include "lwip/tcp.h"
}

class EthernetServer : public arduino::Server {
public:
    explicit EthernetServer(uint16_t port = 80) : _port(port) { }
    ~EthernetServer() { end(); }

    void begin() override;
    void begin(uint16_t port) { _port = port; begin(); }
    void end();

    /* Arduino has two spellings and they mean different things.
     *
     * accept() hands the connection over: the caller owns it, and the server
     * forgets it. available() returns a client that still has data waiting,
     * keeping it in the server's list -- the older Ethernet-library idiom. */
    EthernetClient accept();
    EthernetClient available();

    /* Print, over every connected client at once. The Ethernet library has
     * always had this, and sketches use it for broadcast-style output. */
    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t *buf, size_t size) override;
    using Print::write;

    operator bool() const { return _listen != nullptr; }
    uint16_t port() const { return _port; }

    /* Applied to each accepted connection. Set it before begin(). */
    void setNoDelay(bool on) { _no_delay = on; }
    bool getNoDelay() const { return _no_delay; }

    static const uint8_t MAX_PENDING = 4;

private:
    static err_t _s_accept(void *arg, tcp_pcb *newpcb, err_t err);
    err_t _on_accept(tcp_pcb *newpcb, err_t err);

    tcp_pcb *_listen = nullptr;
    uint16_t _port;
    bool _no_delay = false;

    /* Contexts accepted but not yet collected by the sketch. Refs are held
     * here, so a connection that is never accepted is still closed by end(). */
    LwipClientContext *_pending[MAX_PENDING] = {};
    uint8_t _count = 0;
};
