/* EthernetUDP -- datagrams, on lwIP's raw API.
 *
 * UDP has a shape TCP does not: a packet is a unit. parsePacket() makes
 * exactly one datagram current, read() drains only that one, and the sender's
 * address belongs to it rather than to the socket. Getting this wrong gives a
 * socket that appears to work against one peer and merges packets from two.
 */
#pragma once

#include <Arduino.h>
#include <Udp.h>

extern "C" {
#include "lwip/pbuf.h"
#include "lwip/udp.h"
}

class EthernetUDP : public arduino::UDP {
public:
    EthernetUDP() { }
    ~EthernetUDP() { stop(); }   /* arduino::UDP has no virtual dtor */

    uint8_t begin(uint16_t port) override;
    uint8_t beginMulticast(IPAddress ip, uint16_t port) override;
    void stop() override;

    int beginPacket(IPAddress ip, uint16_t port) override;
    int beginPacket(const char *host, uint16_t port) override;
    int endPacket() override;

    size_t write(uint8_t b) override { return write(&b, 1); }
    size_t write(const uint8_t *buffer, size_t size) override;
    using Print::write;

    int parsePacket() override;
    int available() override;
    int read() override;
    int read(unsigned char *buffer, size_t len) override;
    int read(char *buffer, size_t len) override {
        return read((unsigned char *)buffer, len);
    }
    int peek() override;
    void flush() override;

    IPAddress remoteIP() override { return _remote_ip; }
    uint16_t remotePort() override { return _remote_port; }
    uint16_t localPort() const { return _local_port; }

    /* Bounded, and small. A sketch that stops calling parsePacket() must not
     * be able to exhaust the pbuf pool -- the Ethernet driver needs it to keep
     * receiving anything at all, including the traffic that is not UDP. */
    static const uint8_t MAX_QUEUED = 4;

private:
    struct Datagram {
        pbuf *p;
        ip_addr_t addr;
        uint16_t port;
    };

    static void _s_recv(void *arg, udp_pcb *pcb, pbuf *p,
                        const ip_addr_t *addr, u16_t port);
    void _on_recv(pbuf *p, const ip_addr_t *addr, u16_t port);
    void _drop_current();

    udp_pcb *_pcb = nullptr;
    uint16_t _local_port = 0;

    Datagram _queue[MAX_QUEUED] = {};
    uint8_t _count = 0;

    /* The datagram parsePacket() made current. */
    pbuf *_rx = nullptr;
    uint16_t _rx_offset = 0;
    IPAddress _remote_ip;
    uint16_t _remote_port = 0;

    /* The one being built by beginPacket()/write()/endPacket(). */
    pbuf *_tx = nullptr;
    uint16_t _tx_len = 0;
    ip_addr_t _tx_addr = {};
    uint16_t _tx_port = 0;
};
