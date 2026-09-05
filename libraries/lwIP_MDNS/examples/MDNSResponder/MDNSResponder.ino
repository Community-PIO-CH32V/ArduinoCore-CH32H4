/*
   mDNS: answer to a name instead of an address.

   After this the board answers `ping ch32h4.local` from any machine on the
   same subnet, and a web browser finds http://ch32h4.local/ without anyone
   having to look up what DHCP handed out today.

   This example code is in the public domain.

   ---
   For the CH32H41x core.

   .local IS NOT DNS. Nothing resolves it through a router or a DNS server: it
   is multicast on the local link. Same subnet, or nothing -- across a VLAN or
   a VPN it will simply not answer, and that is not a fault in the board.

   MDNS.begin() must come AFTER Ethernet.begin(): the responder binds to an
   interface, and one that is not up yet has not joined the multicast group.
*/

#include <LwipEthernet.h>
#include <MDNS.h>
#include <WebServer.h>

WebServer server(80);

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  if (Ethernet.begin() == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    return;
  }
  Serial.print("IP address: ");
  Serial.println(Ethernet.localIP());

  if (!MDNS.begin("ch32h4")) {
    Serial.println("MDNS.begin() failed -- is the interface up?");
  } else {
    Serial.println("responding to ch32h4.local");
    /* Advertise the web server, so anything looking for one finds it. */
    MDNS.addService("http", "tcp", 80);
  }

  server.on("/", []() {
    server.send(200, "text/plain", "found me by name\r\n");
  });
  server.begin();
}

void loop() {
  server.handleClient();
  /* Nothing to do -- the responder runs from lwIP's timers, which run from
     yield(). Here so sketches written for ESP8266mDNS compile unchanged. */
  MDNS.update();
}
