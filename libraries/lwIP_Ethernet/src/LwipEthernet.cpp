#include "LwipEthernet.h"

extern "C" {
#include "lwip/dhcp.h"
#include "lwip/init.h"
#include "lwip/netif.h"
#include "lwip/timeouts.h"
}

/* eth_link_status()'s encoding, which is the driver's, not Arduino's:
 *   0 no link, 1 link only, 2 netif up without an address, 3 up with one. */
#define ETH_LS_NO_LINK   0
#define ETH_LS_LINK      1
#define ETH_LS_UP        2
#define ETH_LS_HAS_IP    3

void LwipEthernetClass::setHostname(const char *name) {
    ch32h4_eth_hostname = name;
}

static bool eth_bring_up() {
    /* lwIP first: eth_init() calls netif_add, which needs the stack
     * initialised. Calling it twice is harmless -- lwip_init() is idempotent
     * in the sense that matters here, and eth_init() is only reached once
     * because begin() guards on _started. */
    static bool lwip_started = false;
    if (!lwip_started) {
        lwip_init();
        lwip_started = true;
    }

    if (eth_init(&eth_instance) != 0) {
        return false;
    }
    return eth_start(&eth_instance) == 0;
}

int LwipEthernetClass::begin(unsigned long timeout_ms) {
    if (!_started) {
        if (!eth_bring_up()) {
            return 0;
        }
        _started = true;
    }

    struct netif *n = eth_netif(&eth_instance);
    dhcp_start(n);

    if (timeout_ms == 0) {
        /* Leave DHCP running and let the sketch get on with something else.
         * status() and connected() are how it finds out later. */
        return 1;
    }

    const unsigned long start = millis();
    while (millis() - start < timeout_ms) {
        update();
        if (eth_link_status(&eth_instance) == ETH_LS_HAS_IP) {
            return 1;
        }
        yield();
    }
    return 0;
}

int LwipEthernetClass::begin(IPAddress ip, IPAddress dns, IPAddress gateway,
                             IPAddress subnet) {
    if (!_started) {
        if (!eth_bring_up()) {
            return 0;
        }
        _started = true;
    }

    struct netif *n = eth_netif(&eth_instance);

    /* Stop DHCP before setting a static address, or a lease arriving later
     * would overwrite it -- and the sketch would work for thirty seconds. */
    dhcp_stop(n);

    ip_addr_t a, g, m;
    ip_addr_set_ip4_u32(&a, (uint32_t)ip);
    ip_addr_set_ip4_u32(&g, (uint32_t)gateway);
    ip_addr_set_ip4_u32(&m, (uint32_t)subnet);
    netif_set_addr(n, ip_2_ip4(&a), ip_2_ip4(&m), ip_2_ip4(&g));

#if LWIP_DNS
    ip_addr_t d;
    ip_addr_set_ip4_u32(&d, (uint32_t)dns);
    dns_setserver(0, &d);
#else
    (void)dns;
#endif

    return 1;
}

int LwipEthernetClass::begin(IPAddress ip) {
    /* The conventional guesses: gateway at .1 on the same /24, and it as the
     * DNS server too. A sketch that needs otherwise passes them explicitly. */
    IPAddress gw(ip[0], ip[1], ip[2], 1);
    IPAddress mask(255, 255, 255, 0);
    return begin(ip, gw, gw, mask);
}

void LwipEthernetClass::end() {
    if (!_started) {
        return;
    }
    struct netif *n = eth_netif(&eth_instance);
    dhcp_stop(n);
    eth_stop(&eth_instance);
    _started = false;
}

LwipEthernetClass::LinkStatus LwipEthernetClass::linkStatus() {
    if (!_started) {
        return Unknown;
    }
    return eth_link_is_up(&eth_instance) ? LinkON : LinkOFF;
}

LwipEthernetClass::HardwareStatus LwipEthernetClass::hardwareStatus() {
    return _started ? EthernetCH32H4 : NoHardware;
}

int LwipEthernetClass::status() {
    if (!_started) {
        return 0;
    }
    const int s = eth_link_status(&eth_instance);
    if (s == ETH_LS_HAS_IP) {
        return 2;
    }
    return (s >= ETH_LS_LINK) ? 1 : 0;
}

IPAddress LwipEthernetClass::localIP() {
    if (!_started) {
        return IPAddress(0, 0, 0, 0);
    }
    return IPAddress(netif_ip4_addr(eth_netif(&eth_instance))->addr);
}

IPAddress LwipEthernetClass::subnetMask() {
    if (!_started) {
        return IPAddress(0, 0, 0, 0);
    }
    return IPAddress(netif_ip4_netmask(eth_netif(&eth_instance))->addr);
}

IPAddress LwipEthernetClass::gatewayIP() {
    if (!_started) {
        return IPAddress(0, 0, 0, 0);
    }
    return IPAddress(netif_ip4_gw(eth_netif(&eth_instance))->addr);
}

IPAddress LwipEthernetClass::dnsIP(int n) {
#if LWIP_DNS
    const ip_addr_t *d = dns_getserver(n);
    return d ? IPAddress(ip_2_ip4(d)->addr) : IPAddress(0, 0, 0, 0);
#else
    (void)n;
    return IPAddress(0, 0, 0, 0);
#endif
}

void LwipEthernetClass::macAddress(uint8_t *mac) {
    if (!_started || !mac) {
        return;
    }
    struct netif *n = eth_netif(&eth_instance);
    for (int i = 0; i < 6; i++) {
        mac[i] = n->hwaddr[i];
    }
}

void LwipEthernetClass::update() {
    /* lwIP's timers: DHCP renewal, TCP retransmission, ARP ageing. Nothing
     * else drives them under NO_SYS, so a sketch that never reaches here has a
     * stack that appears to work and then quietly stops renewing its lease. */
    sys_check_timeouts();
}

/* The hook the core's yield() calls. Weak there, strong here, so linking this
 * library is all a sketch has to do. */
extern "C" void ch32h4_net_update(void) {
    LwipEthernetClass::update();
}

LwipEthernetClass Ethernet;
