/* mDNS: answer to a name, and say what you offer.
 *
 *     MDNS.begin("ch32h4");                     // ch32h4.local
 *     MDNS.addService("http", "tcp", 80);
 *
 * After that the board answers `ping ch32h4.local` from any machine on the
 * subnet, and anything looking for an HTTP server finds it. It is also how the
 * Arduino IDE lists a board as a network port: ArduinoOTA advertises
 * `_arduino._tcp` through this.
 *
 * The name is in the shape of ESP8266mDNS and arduino-pico's LEAmDNS, because
 * that is what sketches are written against.
 *
 * NOT TIED TO ANY ONE INTERFACE. It binds to lwIP's default netif, which is
 * whatever interface came up -- this part's Ethernet today, and an ESP-AT or
 * SDIO WiFi netif tomorrow with nothing here to change. begin() also takes an
 * explicit netif for a board with two.
 *
 * TWO THINGS THAT CATCH PEOPLE:
 *
 * `.local` IS NOT DNS. Nothing resolves it through a router or a DNS server:
 * it is multicast on the local link, so it works on the same subnet and
 * nowhere else. Across a VLAN or a VPN it will simply not answer, and that is
 * not a fault in the board.
 *
 * WINDOWS NEEDS A RESPONDER OF ITS OWN. macOS and most Linux desktops resolve
 * .local out of the box (Bonjour, Avahi). Windows 10 and 11 mostly do; an
 * older or locked-down machine may not, in which case the address still works
 * and the name does not.
 *
 * update() exists for API compatibility and does nothing: this responder runs
 * from lwIP's own timers, which run from yield(). A sketch that never yields
 * stops answering, which is the same rule as everything else here.
 */
#pragma once

#include <Arduino.h>

#include "lwip_arduino.h"

struct netif;

class MDNSClass {
public:
    /* Claim <hostname>.local on lwIP's default interface.
     *
     * Returns false when there is no interface up yet -- the responder binds
     * to a netif, and one that is not up has not joined the multicast group,
     * so this would "succeed" and answer nothing. Call it after the interface
     * is up: Ethernet.begin(), or whatever brings yours up.
     *
     * The name is copied, so a stack buffer is fine. */
    bool begin(const char *hostname);
    bool begin(const String &hostname) { return begin(hostname.c_str()); }

    /* A specific interface, for a board with more than one. */
    bool begin(const char *hostname, struct netif *interface);

    void end();

    /* Advertise a service: addService("http", "tcp", 80) publishes
     * _http._tcp.local on port 80.
     *
     * Returns a slot number, or -1 when there is no room -- MDNS_MAX_SERVICES
     * in lwipopts.h is 2, because each one costs RAM and a larger response
     * packet. */
    int addService(const char *service, const char *proto, uint16_t port);
    int addService(const String &service, const String &proto, uint16_t port) {
        return addService(service.c_str(), proto.c_str(), port);
    }

    /* A TXT record on a service addService() returned. Some clients require
     * particular keys -- the Arduino IDE reads `board` and `tcp_check` off
     * _arduino._tcp -- and a client that has already cached the service will
     * not see one added later, so add them straight after addService(). */
    bool addServiceTxt(int slot, const char *key, const char *value);

    /* Advertise _arduino._tcp, which is what makes the board appear under
     * Tools -> Port in the Arduino IDE, and what PlatformIO's mDNS scan finds.
     *
     * `auth` must say whether ArduinoOTA was given a password: the IDE reads
     * it back to decide whether to prompt for one, so a board that requires a
     * password but advertises auth_upload=no gets an upload without one and
     * rejects it, with nothing on screen explaining why.
     *
     * ArduinoOTA::begin() calls this; a sketch rarely needs to. */
    int enableArduino(uint16_t port, bool auth = false);

    /* Nothing to do; the responder runs from lwIP's timers. Here so sketches
     * written for ESP8266mDNS compile unchanged. */
    void update() { }

    const char *hostname() const { return _running ? _hostname : nullptr; }
    bool running() const { return _running; }

private:
    char _hostname[32] = {0};
    bool _running = false;
    int _services = 0;
    struct netif *_netif = nullptr;
};

extern MDNSClass MDNS;
