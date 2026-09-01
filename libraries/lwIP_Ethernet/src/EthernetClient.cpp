#include "EthernetClient.h"

extern "C" {
#include "lwip/dns.h"
#include "lwip/ip_addr.h"
}

int EthernetClient::connect(IPAddress ip, uint16_t port) {
    stop();

    LwipClientContext *ctx = new LwipClientContext();
    if (!ctx) {
        return 0;
    }
    ctx->ref();

    ip_addr_t addr;
    ip_addr_set_ip4_u32(&addr, (uint32_t)ip);

    if (!ctx->connect(&addr, port, (uint32_t)getTimeout())) {
        ctx->unref();
        return 0;
    }
    _ctx = ctx;
    return 1;
}

#if LWIP_DNS
namespace {

/* dns_gethostbyname answers immediately from the cache, or later through this
 * callback. `resolved` distinguishes "not answered yet" from "answered with
 * nothing", which a null address alone cannot. */
struct DnsWait {
    ip_addr_t addr;
    bool resolved;
    bool found;
};

void dns_done(const char *name, const ip_addr_t *addr, void *arg) {
    (void)name;
    DnsWait *w = static_cast<DnsWait *>(arg);
    if (addr) {
        w->addr = *addr;
        w->found = true;
    }
    w->resolved = true;
}

}  // namespace
#endif

int EthernetClient::connect(const char *host, uint16_t port) {
#if LWIP_DNS
    if (!host) {
        return 0;
    }

    DnsWait wait = {};
    const err_t e = dns_gethostbyname(host, &wait.addr, dns_done, &wait);
    if (e == ERR_OK) {
        wait.resolved = wait.found = true;
    } else if (e == ERR_INPROGRESS) {
        const uint32_t start = millis();
        /* The reply arrives through Ethernet::update(), which runs from
         * yield(). Waiting without it never resolves anything. */
        while (!wait.resolved && (millis() - start) < (uint32_t)getTimeout()) {
            yield();
        }
    } else {
        return 0;
    }

    if (!wait.found) {
        return 0;
    }
    return connect(IPAddress(ip_2_ip4(&wait.addr)->addr), port);
#else
    (void)host;
    (void)port;
    return 0;
#endif
}

size_t EthernetClient::write(const uint8_t *buf, size_t size) {
    if (!_ctx) {
        return 0;
    }
    return _ctx->write(buf, size);
}

int EthernetClient::available() {
    if (!_ctx) {
        return 0;
    }
    /* Pump first. A sketch polling available() in a tight loop without calling
     * yield() itself is the commonest shape there is, and without this it
     * would poll a stack that is never given the chance to receive anything. */
    yield();
    return (int)_ctx->available();
}

int EthernetClient::read() {
    uint8_t b;
    if (!_ctx || _ctx->read(&b, 1) != 1) {
        return -1;
    }
    return b;
}

int EthernetClient::read(uint8_t *buf, size_t size) {
    if (!_ctx) {
        return -1;
    }
    const size_t n = _ctx->read(buf, size);
    return n ? (int)n : -1;
}

int EthernetClient::peek() {
    return _ctx ? _ctx->peek() : -1;
}

void EthernetClient::flush() {
    if (_ctx) {
        _ctx->flush();
    }
}

void EthernetClient::stop() {
    if (_ctx) {
        _ctx->close();
        _ctx->unref();
        _ctx = nullptr;
    }
}

uint8_t EthernetClient::connected() {
    if (!_ctx) {
        return 0;
    }
    return _ctx->connected() ? 1 : 0;
}
