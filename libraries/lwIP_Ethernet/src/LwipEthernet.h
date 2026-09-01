/* Ethernet, over lwIP, on the CH32H41x's on-chip MAC and 100M PHY.
 *
 * The structure follows arduino-pico's lwIP_* libraries so that client, server
 * and UDP classes can be shared rather than reimplemented per interface. The
 * difference here is that the device is a native MAC with its own netif, not
 * an SPI chip polled by LwipIntfDev -- so this class drives ch32h4_eth.c
 * directly instead of wrapping a RawDev.
 *
 * WHICH CORE. lwIP under NO_SYS has no locking; the stack belongs to the V5F,
 * where setup() and loop() run. Calling any of this from setup1()/loop1() trips
 * LWIP_ASSERT_CORE_LOCKED and halts with a message, which is a great deal
 * better than the pbuf corruption it prevents.
 *
 * THE CRYSTAL. The Ethernet PLL will not lock on the internal RC. begin()
 * reports that as its own failure rather than as a link-down, because the two
 * look identical from the outside and lead to completely different places.
 */
#pragma once

#include <Arduino.h>
#include <IPAddress.h>

extern "C" {
#include "ch32h4_eth.h"
}

class LwipEthernetClass {
public:
    /* DHCP. Returns 1 on success, 0 on failure, matching the Arduino Ethernet
     * library so sketches written against it behave the same.
     *
     * `timeout_ms` bounds the wait for a lease. 0 returns as soon as the
     * interface is up and leaves DHCP running in the background, which is what
     * a sketch that wants to do something else meanwhile should use. */
    int begin(unsigned long timeout_ms = 15000);

    /* Static addressing. No DHCP, so this returns as soon as the interface is
     * up. */
    int begin(IPAddress ip, IPAddress dns, IPAddress gateway, IPAddress subnet);
    int begin(IPAddress ip);

    void end();

    /* The DHCP hostname, and so what appears in a router's client list. Must be
     * called before begin(); lwIP stores the pointer, so pass a string that
     * outlives the interface -- a literal or a global, not a stack buffer. */
    void setHostname(const char *name);

    /* EthernetLinkStatus, as the Arduino library spells it. */
    enum LinkStatus { Unknown, LinkON, LinkOFF };
    LinkStatus linkStatus();

    /* EthernetHardwareStatus. Reports whether the MAC came up at all -- which
     * on this part is mostly a question about the crystal. */
    enum HardwareStatus { NoHardware, EthernetCH32H4 = 6 };
    HardwareStatus hardwareStatus();

    /* 0 = no link, 1 = link but no address, 2 = link and an address. */
    int status();

    IPAddress localIP();
    IPAddress subnetMask();
    IPAddress gatewayIP();
    IPAddress dnsIP(int n = 0);

    /* The MAC address, which this part carries in its option bytes -- so two
     * boards do not collide and nothing has to be hard-coded. */
    void macAddress(uint8_t *mac);

    /* Run lwIP's timers and drain the receive path. Called from yield(), so a
     * sketch does not have to -- but a sketch that spins without yielding will
     * not service the network, which is the one thing to know. */
    static void update();

    /* True once the interface has an address. */
    bool connected() { return status() == 2; }

private:
    bool _started = false;
};

extern LwipEthernetClass Ethernet;

/* Including this header is all a sketch should have to do. The Arduino
 * Ethernet library has always been one include for the interface, the client,
 * the server and UDP, and a sketch ported from it must not have to learn that
 * this core split them into four files. */
#include "EthernetClient.h"
#include "EthernetServer.h"
#include "EthernetUdp.h"
#include "NTP.h"

/* The names the Arduino Ethernet library uses. EthernetUDP is already spelled
 * that way; this is here so `EthernetUdp` compiles too, which some sketches
 * and a good deal of documentation use. */
using EthernetUdp = EthernetUDP;
