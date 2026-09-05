#include "MDNS.h"

#include <string.h>

extern "C" {
#include "lwip/apps/mdns.h"
#include "lwip/netif.h"
}

/* Bound to a netif, not to a driver. lwIP's mDNS is per-interface, and
 * netif_default is whatever interface came up -- so this works unchanged for
 * any future one. */

static enum mdns_sd_proto proto_of(const char *proto) {
    /* "tcp" or "udp", with or without the underscore Bonjour writes. */
    if (proto && proto[0] == '_') {
        proto++;
    }
    if (proto && (proto[0] == 'u' || proto[0] == 'U')) {
        return DNSSD_PROTO_UDP;
    }
    return DNSSD_PROTO_TCP;
}

/* lwIP asks for TXT records through a callback rather than storing them, so
 * each service keeps its own list and hands it over on demand. */
struct TxtList {
    static const int MAX = 6;
    char item[MAX][64];
    int count;
};

static TxtList s_txt[4];

static void txt_callback(struct mdns_service *service, void *userdata) {
    const int slot = (int)(intptr_t)userdata;
    if (slot < 0 || slot >= 4) {
        return;
    }
    for (int i = 0; i < s_txt[slot].count; i++) {
        const char *t = s_txt[slot].item[i];
        mdns_resp_add_service_txtitem(service, t, (u8_t)strlen(t));
    }
}

bool MDNSClass::begin(const char *hostname) {
    return begin(hostname, netif_default);
}

bool MDNSClass::begin(const char *hostname, struct netif *interface) {
    if (!hostname || !hostname[0]) {
        return false;
    }
    if (_running) {
        end();
    }
    if (interface == nullptr || !netif_is_up(interface)) {
        return false;
    }

    strncpy(_hostname, hostname, sizeof(_hostname) - 1);
    _hostname[sizeof(_hostname) - 1] = '\0';

    /* Idempotent in lwIP and cheap, so a sketch does not have to know it
     * exists. */
    mdns_resp_init();

    if (mdns_resp_add_netif(interface, _hostname) != ERR_OK) {
        _hostname[0] = '\0';
        return false;
    }

    _netif = interface;
    _running = true;
    _services = 0;
    for (int i = 0; i < 4; i++) {
        s_txt[i].count = 0;
    }
    return true;
}

void MDNSClass::end() {
    if (!_running) {
        return;
    }
    if (_netif) {
        mdns_resp_remove_netif(_netif);
    }
    _netif = nullptr;
    _running = false;
    _services = 0;
    _hostname[0] = '\0';
}

int MDNSClass::addService(const char *service, const char *proto,
                          uint16_t port) {
    if (!_running || _netif == nullptr || _services >= 4) {
        return -1;
    }

    /* Leading underscores are how the records are WRITTEN and not what lwIP
     * wants -- it adds its own. Passing "_http" here advertises
     * "__http._tcp", which resolves for nobody. */
    if (service && service[0] == '_') {
        service++;
    }

    const int slot = _services;
    const s8_t id = mdns_resp_add_service(_netif, _hostname, service,
                                          proto_of(proto), port,
                                          txt_callback,
                                          (void *)(intptr_t)slot);
    if (id < 0) {
        return -1;
    }
    _services++;
    mdns_resp_announce(_netif);
    return slot;
}

bool MDNSClass::addServiceTxt(int slot, const char *key, const char *value) {
    if (slot < 0 || slot >= _services || !key) {
        return false;
    }
    TxtList &t = s_txt[slot];
    if (t.count >= TxtList::MAX) {
        return false;
    }
    /* "key=value" -- a TXT item is one string on the wire. */
    snprintf(t.item[t.count], sizeof(t.item[0]), "%s=%s", key,
             value ? value : "");
    t.count++;

    if (_netif) {
        /* Re-announce, or a client that already cached the service never sees
         * the new record. */
        mdns_resp_announce(_netif);
    }
    return true;
}

/* The Arduino IDE and this core's PlatformIO builder both define this. A
 * hand-rolled build that does not still has to produce a listable board. */
#ifndef ARDUINO_BOARD
#define ARDUINO_BOARD "CH32H4"
#endif

int MDNSClass::enableArduino(uint16_t port, bool auth) {
    const int slot = addService("arduino", "tcp", port);
    if (slot < 0) {
        return -1;
    }

    /* The four keys the IDE's port discovery reads. It will list a board that
     * advertises _arduino._tcp without them, but it decides how to talk to it
     * from these -- get auth_upload wrong and it uploads without asking for
     * the password, then the board rejects it. */
    addServiceTxt(slot, "board", ARDUINO_BOARD);
    addServiceTxt(slot, "tcp_check", "no");
    addServiceTxt(slot, "ssh_upload", "no");
    addServiceTxt(slot, "auth_upload", auth ? "yes" : "no");
    return slot;
}

MDNSClass MDNS;
