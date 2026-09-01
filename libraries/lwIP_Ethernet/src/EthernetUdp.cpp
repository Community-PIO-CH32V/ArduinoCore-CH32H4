#include "EthernetUdp.h"

extern "C" {
#include "lwip/dns.h"
#include "lwip/igmp.h"
#include "lwip/ip_addr.h"
}

uint8_t EthernetUDP::begin(uint16_t port) {
    stop();
    _pcb = udp_new();
    if (!_pcb) {
        return 0;
    }
    if (udp_bind(_pcb, IP_ANY_TYPE, port) != ERR_OK) {
        udp_remove(_pcb);
        _pcb = nullptr;
        return 0;
    }
    udp_recv(_pcb, _s_recv, this);
    _local_port = port;
    return 1;
}

uint8_t EthernetUDP::beginMulticast(IPAddress ip, uint16_t port) {
#if LWIP_IGMP
    if (!begin(port)) {
        return 0;
    }
    ip_addr_t group;
    ip_addr_set_ip4_u32(&group, (uint32_t)ip);
    /* Joining the group is what makes the MAC accept the frames. Binding
     * alone gives a socket that is open and receives nothing. */
    if (igmp_joingroup(IP4_ADDR_ANY4, ip_2_ip4(&group)) != ERR_OK) {
        stop();
        return 0;
    }
    return 1;
#else
    (void)ip;
    (void)port;
    return 0;
#endif
}

void EthernetUDP::stop() {
    if (_pcb) {
        udp_recv(_pcb, nullptr, nullptr);
        udp_remove(_pcb);
        _pcb = nullptr;
    }
    for (uint8_t i = 0; i < _count; i++) {
        pbuf_free(_queue[i].p);
        _queue[i].p = nullptr;
    }
    _count = 0;
    _drop_current();
    if (_tx) {
        pbuf_free(_tx);
        _tx = nullptr;
    }
    _tx_len = 0;
    _local_port = 0;
}

void EthernetUDP::_drop_current() {
    if (_rx) {
        pbuf_free(_rx);
        _rx = nullptr;
    }
    _rx_offset = 0;
}

void EthernetUDP::_s_recv(void *arg, udp_pcb *pcb, pbuf *p,
                          const ip_addr_t *addr, u16_t port) {
    (void)pcb;
    static_cast<EthernetUDP *>(arg)->_on_recv(p, addr, port);
}

void EthernetUDP::_on_recv(pbuf *p, const ip_addr_t *addr, u16_t port) {
    if (!p) {
        return;
    }
    if (_count >= MAX_QUEUED) {
        /* Drop the newest rather than growing. The pbuf pool is shared with
         * the Ethernet driver's receive path: a sketch that stopped calling
         * parsePacket() would otherwise take the whole interface down, not
         * just its own socket. */
        pbuf_free(p);
        return;
    }
    _queue[_count].p = p;
    _queue[_count].addr = *addr;
    _queue[_count].port = port;
    _count++;
}

int EthernetUDP::parsePacket() {
    /* Pump first: a sketch polling parsePacket() without calling yield()
     * itself is the standard shape, and nothing would ever arrive. */
    yield();

    /* Whatever the last packet left unread is discarded here, not carried
     * over. A datagram is a unit; merging the tail of one into the next is
     * how a UDP socket comes to work against one peer and not two. */
    _drop_current();

    if (_count == 0) {
        return 0;
    }

    _rx = _queue[0].p;
    _remote_ip = IPAddress(ip_2_ip4(&_queue[0].addr)->addr);
    _remote_port = _queue[0].port;
    for (uint8_t i = 1; i < _count; i++) {
        _queue[i - 1] = _queue[i];
    }
    _count--;
    _queue[_count].p = nullptr;

    _rx_offset = 0;
    return _rx->tot_len;
}

int EthernetUDP::available() {
    if (!_rx) {
        return 0;
    }
    return _rx->tot_len - _rx_offset;
}

int EthernetUDP::read() {
    uint8_t b;
    if (read(&b, 1) != 1) {
        return -1;
    }
    return b;
}

int EthernetUDP::read(unsigned char *buffer, size_t len) {
    const int have = available();
    if (have <= 0) {
        return -1;
    }
    if (len > (size_t)have) {
        len = (size_t)have;
    }
    if (buffer) {
        pbuf_copy_partial(_rx, buffer, (uint16_t)len, _rx_offset);
    }
    _rx_offset += (uint16_t)len;
    return (int)len;
}

int EthernetUDP::peek() {
    if (available() <= 0) {
        return -1;
    }
    return pbuf_get_at(_rx, _rx_offset);
}

void EthernetUDP::flush() {
    /* Arduino's UDP::flush means "done with this packet", not "push the
     * transmit queue". Discarding the rest of the current datagram is the
     * whole operation. */
    _drop_current();
}

int EthernetUDP::beginPacket(IPAddress ip, uint16_t port) {
    if (!_pcb) {
        /* Sending without begin() is ordinary -- a sketch that only sends has
         * no port to bind. Take an ephemeral one. */
        if (!begin(0)) {
            return 0;
        }
    }
    if (_tx) {
        pbuf_free(_tx);
        _tx = nullptr;
    }
    _tx_len = 0;
    ip_addr_set_ip4_u32(&_tx_addr, (uint32_t)ip);
    _tx_port = port;
    return 1;
}

int EthernetUDP::beginPacket(const char *host, uint16_t port) {
#if LWIP_DNS
    if (!host) {
        return 0;
    }
    ip_addr_t addr;
    /* Cache only. A blocking resolve here would have to pump the stack from
     * inside what the sketch believes is a buffer-filling call; sketches that
     * need a name resolved should use EthernetClient::connect, which does it
     * properly, or resolve once and keep the address. */
    if (dns_gethostbyname(host, &addr, nullptr, nullptr) != ERR_OK) {
        return 0;
    }
    return beginPacket(IPAddress(ip_2_ip4(&addr)->addr), port);
#else
    (void)host;
    (void)port;
    return 0;
#endif
}

size_t EthernetUDP::write(const uint8_t *buffer, size_t size) {
    if (!_pcb || size == 0) {
        return 0;
    }
    /* Grow the datagram as the sketch writes into it. PBUF_RAM is one
     * contiguous allocation, so this is a realloc-and-copy; datagrams are
     * small and written in a handful of calls, which is what makes that
     * acceptable here and not in the TCP path. */
    const uint16_t need = (uint16_t)(_tx_len + size);
    pbuf *grown = pbuf_alloc(PBUF_TRANSPORT, need, PBUF_RAM);
    if (!grown) {
        return 0;
    }
    if (_tx) {
        pbuf_copy_partial(_tx, grown->payload, _tx_len, 0);
        pbuf_free(_tx);
    }
    memcpy((uint8_t *)grown->payload + _tx_len, buffer, size);
    _tx = grown;
    _tx_len = need;
    return size;
}

int EthernetUDP::endPacket() {
    if (!_pcb) {
        return 0;
    }
    /* A zero-length datagram is legal and occasionally meaningful, so an
     * empty _tx is sent rather than refused. */
    pbuf *p = _tx;
    bool temp = false;
    if (!p) {
        p = pbuf_alloc(PBUF_TRANSPORT, 0, PBUF_RAM);
        if (!p) {
            return 0;
        }
        temp = true;
    }

    const err_t e = udp_sendto(_pcb, p, &_tx_addr, _tx_port);

    pbuf_free(p);
    if (!temp) {
        _tx = nullptr;
        _tx_len = 0;
    }
    return (e == ERR_OK) ? 1 : 0;
}
