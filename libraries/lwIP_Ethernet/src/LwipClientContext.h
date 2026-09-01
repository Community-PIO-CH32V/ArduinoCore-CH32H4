/* One TCP connection, on lwIP's raw API.
 *
 * The shape is arduino-pico's (and esp8266's before it): a refcounted context
 * object holding the pcb and the receive chain, with a thin Arduino Client on
 * top. Copying an EthernetClient copies a pointer, not a connection -- which
 * is what sketches assume when they pass a client to a function by value.
 *
 * Under NO_SYS=1 every lwIP callback arrives from Ethernet::update(), which
 * runs from yield(). So anything here that waits MUST call yield(), or the
 * data it is waiting for can never arrive. That is the single rule this file
 * lives by, and it is why connect() and the flush paths look the way they do.
 */
#pragma once

#include <Arduino.h>

extern "C" {
#include "lwip/err.h"
#include "lwip/pbuf.h"
#include "lwip/tcp.h"
}

class LwipClientContext {
public:
    /* Takes ownership of an already-connected pcb -- the server path. */
    explicit LwipClientContext(tcp_pcb *pcb)
        : _pcb(pcb), _state(CONNECTED) {
        _attach();
    }

    /* An unconnected context, for the client path. */
    LwipClientContext() { }

    ~LwipClientContext() {
        close();
        _discard_rx();
    }

    LwipClientContext(const LwipClientContext &) = delete;
    LwipClientContext &operator=(const LwipClientContext &) = delete;

    int ref() { return ++_refs; }
    int unref() {
        if (--_refs == 0) {
            delete this;
            return 0;
        }
        return _refs;
    }

    /* ---- connecting ---------------------------------------------------- */

    bool connect(const ip_addr_t *addr, uint16_t port, uint32_t timeout_ms) {
        if (_pcb) {
            return false;
        }
        _pcb = tcp_new();
        if (!_pcb) {
            return false;
        }
        _state = CONNECTING;
        _attach();

        if (tcp_connect(_pcb, addr, port, _s_connected) != ERR_OK) {
            _abandon();
            return false;
        }

        const uint32_t start = millis();
        while (_state == CONNECTING && (millis() - start) < timeout_ms) {
            /* The SYN-ACK arrives through Ethernet::update(), which runs from
             * here. Spinning without this waits forever by construction. */
            yield();
        }
        if (_state != CONNECTED) {
            _abandon();
            return false;
        }
        return true;
    }

    /* ---- reading ------------------------------------------------------- */

    size_t available() const {
        size_t n = 0;
        for (const pbuf *p = _rx; p; p = p->next) {
            n += p->len;
        }
        return n - _rx_offset;
    }

    int peek() const {
        if (!_rx || available() == 0) {
            return -1;
        }
        return pbuf_get_at(_rx, (uint16_t)_rx_offset);
    }

    size_t read(uint8_t *dst, size_t size) {
        const size_t have = available();
        if (have == 0) {
            return 0;
        }
        if (size > have) {
            size = have;
        }
        if (dst) {
            pbuf_copy_partial(_rx, dst, (uint16_t)size, (uint16_t)_rx_offset);
        }
        _consume(size);
        return size;
    }

    /* ---- writing ------------------------------------------------------- */

    size_t write(const uint8_t *src, size_t size) {
        if (!_pcb || _state != CONNECTED || size == 0) {
            return 0;
        }
        size_t written = 0;
        const uint32_t start = millis();

        while (written < size) {
            size_t chunk = size - written;
            const size_t room = tcp_sndbuf(_pcb);
            if (room == 0) {
                /* The window is full. Let the stack run so the ACKs that free
                 * it can arrive, and give up rather than block forever if the
                 * peer has stopped reading. */
                if ((millis() - start) > _write_timeout_ms) {
                    break;
                }
                yield();
                if (!_pcb || _state != CONNECTED) {
                    break;
                }
                continue;
            }
            if (chunk > room) {
                chunk = room;
            }

            /* COPY, not a reference into the caller's buffer: an Arduino
             * sketch is entitled to reuse or free that buffer the instant
             * write() returns, and a zero-copy write would put whatever
             * replaced it on the wire. */
            const err_t e = tcp_write(_pcb, src + written, (uint16_t)chunk,
                                      TCP_WRITE_FLAG_COPY);
            if (e == ERR_MEM) {
                yield();
                continue;
            }
            if (e != ERR_OK) {
                break;
            }
            written += chunk;
        }

        if (written && _pcb) {
            tcp_output(_pcb);
        }
        return written;
    }

    /* Push what is queued and wait for the peer to acknowledge it. */
    void flush(uint32_t timeout_ms = 5000) {
        if (!_pcb) {
            return;
        }
        tcp_output(_pcb);
        const uint32_t start = millis();
        while (_pcb && tcp_sndbuf(_pcb) != TCP_SND_BUF
               && (millis() - start) < timeout_ms) {
            yield();
        }
    }

    /* ---- state --------------------------------------------------------- */

    bool connected() const {
        /* Data still buffered counts as connected. A sketch that does
         *     while (client.connected()) { if (client.available()) ... }
         * would otherwise drop the last response of every half-closed
         * connection -- which is most HTTP responses. */
        return _state == CONNECTED || available() > 0;
    }

    uint8_t status() const { return (uint8_t)_state; }

    IPAddress remoteIP() const {
        if (!_pcb) {
            return IPAddress(0, 0, 0, 0);
        }
        return IPAddress(ip_2_ip4(&_pcb->remote_ip)->addr);
    }
    uint16_t remotePort() const { return _pcb ? _pcb->remote_port : 0; }
    IPAddress localIP() const {
        if (!_pcb) {
            return IPAddress(0, 0, 0, 0);
        }
        return IPAddress(ip_2_ip4(&_pcb->local_ip)->addr);
    }
    uint16_t localPort() const { return _pcb ? _pcb->local_port : 0; }

    void setNoDelay(bool on) {
        if (!_pcb) {
            return;
        }
        if (on) {
            tcp_nagle_disable(_pcb);
        } else {
            tcp_nagle_enable(_pcb);
        }
    }
    bool getNoDelay() const { return _pcb && tcp_nagle_disabled(_pcb); }

    void setTimeout(uint32_t ms) { _write_timeout_ms = ms; }

    void close() {
        if (!_pcb) {
            _state = CLOSED;
            return;
        }
        _detach();
        /* tcp_close can fail for want of memory to send the FIN. Aborting is
         * the honest fallback: leaving the pcb attached with its callbacks
         * cleared means lwIP delivers into a freed context later. */
        if (tcp_close(_pcb) != ERR_OK) {
            tcp_abort(_pcb);
        }
        _pcb = nullptr;
        _state = CLOSED;
    }

    void abort() {
        if (_pcb) {
            _detach();
            tcp_abort(_pcb);
            _pcb = nullptr;
        }
        _state = CLOSED;
    }

private:
    enum State : uint8_t { CLOSED = 0, CONNECTING = 1, CONNECTED = 2 };

    void _attach() {
        tcp_arg(_pcb, this);
        tcp_recv(_pcb, _s_recv);
        tcp_err(_pcb, _s_err);
        tcp_sent(_pcb, _s_sent);
        tcp_poll(_pcb, _s_poll, 4);   /* ~2 s: the poll tick is 500 ms */
    }

    void _detach() {
        if (!_pcb) {
            return;
        }
        tcp_arg(_pcb, nullptr);
        tcp_recv(_pcb, nullptr);
        tcp_err(_pcb, nullptr);
        tcp_sent(_pcb, nullptr);
        tcp_poll(_pcb, nullptr, 0);
    }

    void _abandon() {
        if (_pcb) {
            _detach();
            tcp_abort(_pcb);
            _pcb = nullptr;
        }
        _state = CLOSED;
    }

    void _consume(size_t size) {
        _rx_offset += size;
        /* Tell lwIP the window is free again as whole pbufs are retired, not
         * per byte: tcp_recved on every read() of one character would put a
         * window update on the wire for each one. */
        size_t freed = 0;
        while (_rx && _rx_offset >= _rx->len) {
            _rx_offset -= _rx->len;
            freed += _rx->len;
            pbuf *next = _rx->next;
            if (next) {
                pbuf_ref(next);
            }
            _rx->next = nullptr;
            pbuf_free(_rx);
            _rx = next;
        }
        if (freed && _pcb) {
            tcp_recved(_pcb, (uint16_t)freed);
        }
    }

    void _discard_rx() {
        if (_rx) {
            pbuf_free(_rx);
            _rx = nullptr;
        }
        _rx_offset = 0;
    }

    /* ---- lwIP callbacks ------------------------------------------------ */

    err_t _on_recv(pbuf *p, err_t err) {
        if (p == nullptr || err != ERR_OK) {
            /* The peer sent FIN. Keep whatever is already buffered -- see
             * connected() -- and let the sketch drain it. */
            if (p) {
                pbuf_free(p);
            }
            _state = CLOSED;
            return ERR_OK;
        }
        if (_rx) {
            pbuf_cat(_rx, p);
        } else {
            _rx = p;
        }
        return ERR_OK;
    }

    void _on_err(err_t err) {
        /* lwIP has already freed the pcb by the time this runs. Touching it
         * here -- including tcp_abort or tcp_close -- is a use-after-free. */
        (void)err;
        _pcb = nullptr;
        _state = CLOSED;
    }

    static err_t _s_recv(void *arg, tcp_pcb *pcb, pbuf *p, err_t err) {
        (void)pcb;
        return static_cast<LwipClientContext *>(arg)->_on_recv(p, err);
    }
    static void _s_err(void *arg, err_t err) {
        static_cast<LwipClientContext *>(arg)->_on_err(err);
    }
    static err_t _s_sent(void *arg, tcp_pcb *pcb, u16_t len) {
        (void)arg; (void)pcb; (void)len;
        return ERR_OK;
    }
    static err_t _s_poll(void *arg, tcp_pcb *pcb) {
        (void)arg; (void)pcb;
        return ERR_OK;
    }
    static err_t _s_connected(void *arg, tcp_pcb *pcb, err_t err) {
        (void)pcb;
        LwipClientContext *self = static_cast<LwipClientContext *>(arg);
        self->_state = (err == ERR_OK) ? CONNECTED : CLOSED;
        return ERR_OK;
    }

    tcp_pcb *_pcb = nullptr;
    pbuf *_rx = nullptr;
    size_t _rx_offset = 0;
    State _state = CLOSED;
    int _refs = 0;
    uint32_t _write_timeout_ms = 5000;
};
