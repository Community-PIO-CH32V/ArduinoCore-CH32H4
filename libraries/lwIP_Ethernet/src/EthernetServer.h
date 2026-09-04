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
    /* What kind of client this server hands out. Named because WebServer is a
     * template over the server type and asks it this -- the same arrangement
     * arduino-pico and the esp8266 core use, so their WebServer ports over
     * with the typedef supplied rather than the library edited. */
    using ClientType = EthernetClient;

    explicit EthernetServer(uint16_t port = 80) : _port(port) { }

    /* Bind to one local address rather than every interface.
     *
     * Accepted for source compatibility with the WiFi servers WebServer was
     * written against, and honoured: on a board with one interface the only
     * addresses that can be given are that interface's own or INADDR_ANY, and
     * both do the same thing -- but a sketch that names an address and
     * silently gets a server on all of them is worth not writing. */
    EthernetServer(IPAddress addr, uint16_t port) : _addr(addr), _port(port) { }

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

    /* close() is stop() under the name the Server interface and WebServer
       both use. Same thing; two spellings exist for history. */
    void close() { end(); }
    void stop() { end(); }

    /* Both of these exist for WebServer, which uses them to decide how long
       to hold a connection open waiting for a request body.

       A server with another client already holding data, or with its pending
       queue full, cannot afford to wait out the full timeout on a client that
       has gone quiet -- doing so is what makes a single stalled connection
       lock out every other one. WebServer drops the quiet client early when
       either is true, so answering these honestly is what keeps a browser
       that opened a speculative connection from blocking the next request. */
    bool hasClientData() const;
    bool hasMaxPendingClients() const { return _count >= MAX_PENDING; }

    /* Applied to each accepted connection. Set it before begin(). */
    void setNoDelay(bool on) { _no_delay = on; }
    bool getNoDelay() const { return _no_delay; }

    static const uint8_t MAX_PENDING = 4;

private:
    static err_t _s_accept(void *arg, tcp_pcb *newpcb, err_t err);
    err_t _on_accept(tcp_pcb *newpcb, err_t err);

    tcp_pcb *_listen = nullptr;
    IPAddress _addr = IPAddress((uint32_t)0);   /* 0.0.0.0 = every interface */
    uint16_t _port;
    bool _no_delay = false;

    /* Contexts accepted but not yet collected by the sketch. Refs are held
     * here, so a connection that is never accepted is still closed by end(). */
    LwipClientContext *_pending[MAX_PENDING] = {};
    uint8_t _count = 0;
};
