#include "EthernetServer.h"

void EthernetServer::begin() {
    end();

    tcp_pcb *pcb = tcp_new();
    if (!pcb) {
        return;
    }

    /* SO_REUSEADDR, so a restart does not have to wait out TIME_WAIT on the
     * previous incarnation's connections. Without it, re-running a sketch
     * fails to bind for a couple of minutes and looks like a broken server. */
    ip_set_option(pcb, SOF_REUSEADDR);

    /* IP_ANY_TYPE binds every interface and every address family; a named
     * address binds just that one. The distinction only matters on a board
     * with more than one interface, but honouring it costs three lines and
     * the alternative is a constructor argument that silently does nothing. */
    ip_addr_t bind_addr;
    const bool any = ((uint32_t)_addr == 0);
    if (!any) {
        IP4_ADDR(ip_2_ip4(&bind_addr), _addr[0], _addr[1], _addr[2], _addr[3]);
        IP_SET_TYPE_VAL(bind_addr, IPADDR_TYPE_V4);
    }

    if (tcp_bind(pcb, any ? IP_ANY_TYPE : &bind_addr, _port) != ERR_OK) {
        tcp_close(pcb);
        return;
    }

    /* tcp_listen returns a NEW, smaller pcb and frees the one passed in. The
     * original must not be touched afterwards, and must not be closed on the
     * failure path either -- tcp_listen_with_backlog_and_err frees it itself
     * only on success. */
    err_t err = ERR_OK;
    tcp_pcb *listener = tcp_listen_with_backlog_and_err(pcb, MAX_PENDING, &err);
    if (!listener) {
        tcp_close(pcb);
        return;
    }

    _listen = listener;
    tcp_arg(_listen, this);
    tcp_accept(_listen, _s_accept);
}

void EthernetServer::end() {
    if (_listen) {
        tcp_arg(_listen, nullptr);
        tcp_accept(_listen, nullptr);
        tcp_close(_listen);
        _listen = nullptr;
    }
    /* Anything accepted but never collected is this object's to close. */
    for (uint8_t i = 0; i < _count; i++) {
        _pending[i]->close();
        _pending[i]->unref();
        _pending[i] = nullptr;
    }
    _count = 0;
}

err_t EthernetServer::_s_accept(void *arg, tcp_pcb *newpcb, err_t err) {
    return static_cast<EthernetServer *>(arg)->_on_accept(newpcb, err);
}

err_t EthernetServer::_on_accept(tcp_pcb *newpcb, err_t err) {
    if (err != ERR_OK || newpcb == nullptr) {
        return ERR_VAL;
    }
    if (_count >= MAX_PENDING) {
        /* Refuse rather than queue without bound. ERR_MEM makes lwIP hold the
         * SYN, so the peer retries instead of seeing a connection accepted and
         * then dropped. */
        return ERR_MEM;
    }

    LwipClientContext *ctx = new LwipClientContext(newpcb);
    if (!ctx) {
        return ERR_MEM;
    }
    ctx->ref();
    if (_no_delay) {
        ctx->setNoDelay(true);
    }

    _pending[_count++] = ctx;

    /* Tell lwIP this listener has taken one, or its backlog accounting drifts
     * and it eventually stops accepting. */
    tcp_accepted(_listen);
    return ERR_OK;
}

EthernetClient EthernetServer::accept() {
    yield();                       /* let the accept callback actually run */
    if (_count == 0) {
        return EthernetClient();
    }
    LwipClientContext *ctx = _pending[0];
    for (uint8_t i = 1; i < _count; i++) {
        _pending[i - 1] = _pending[i];
    }
    _count--;
    _pending[_count] = nullptr;

    EthernetClient c(ctx);         /* takes its own ref */
    ctx->unref();                  /* drop the queue's */
    return c;
}

EthernetClient EthernetServer::available() {
    yield();
    /* The older idiom: return a client that has something to read, and keep
     * it. Drop any that closed with nothing left, or the queue fills with
     * dead connections and the server stops accepting. */
    uint8_t w = 0;
    for (uint8_t i = 0; i < _count; i++) {
        LwipClientContext *ctx = _pending[i];
        if (ctx->connected() || ctx->available()) {
            _pending[w++] = ctx;
        } else {
            ctx->close();
            ctx->unref();
        }
    }
    for (uint8_t i = w; i < _count; i++) {
        _pending[i] = nullptr;
    }
    _count = w;

    for (uint8_t i = 0; i < _count; i++) {
        if (_pending[i]->available()) {
            return EthernetClient(_pending[i]);
        }
    }
    return EthernetClient();
}

size_t EthernetServer::write(const uint8_t *buf, size_t size) {
    size_t n = 0;
    for (uint8_t i = 0; i < _count; i++) {
        if (_pending[i]->connected()) {
            n = _pending[i]->write(buf, size);
        }
    }
    return n;
}

bool EthernetServer::hasClientData() const {
    /* Any queued connection with bytes already waiting. Not "any queued
       connection": a client that has connected and said nothing is exactly
       the case WebServer is willing to keep waiting on. */
    for (uint8_t i = 0; i < _count; i++) {
        if (_pending[i] && _pending[i]->available() > 0) {
            return true;
        }
    }
    return false;
}
