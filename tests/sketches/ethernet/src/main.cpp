#include <Arduino.h>
#include <LwipEthernet.h>

void setup() {
  Serial1.begin(115200);
  Serial1.println("ethtest starting");

  Ethernet.setHostname("ch32h417-arduino");

  /* DHCP, with a bounded wait. The board's RJ45 needs a network with a DHCP
     server; without one this reports failure rather than hanging. */
  int ok = Ethernet.begin(15000);
  Serial1.print("dhcp="); Serial1.println(ok);
  Serial1.print("hw="); Serial1.println((int)Ethernet.hardwareStatus());
  Serial1.print("link="); Serial1.println((int)Ethernet.linkStatus());
  Serial1.print("ip="); Serial1.println(Ethernet.localIP());
  Serial1.print("gw="); Serial1.println(Ethernet.gatewayIP());
  Serial1.print("mask="); Serial1.println(Ethernet.subnetMask());

  uint8_t mac[6];
  Ethernet.macAddress(mac);
  Serial1.print("mac=");
  for (int i = 0; i < 6; i++) {
    if (i) Serial1.print(":");
    if (mac[i] < 16) Serial1.print("0");
    Serial1.print(mac[i], HEX);
  }
  Serial1.println();
  Serial1.println("ethtest ready");
  Serial1.print("> ");
}

void loop() {
  static String line;
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        if (line == "netstat") {
          Serial1.print("status="); Serial1.println(Ethernet.status());
          Serial1.print("link="); Serial1.println((int)Ethernet.linkStatus());
          Serial1.print("ip="); Serial1.println(Ethernet.localIP());
          const eth_stats_t *s = eth_get_stats(&eth_instance);
          Serial1.print("rx_frames="); Serial1.println(s->rx_frames);
          Serial1.print("tx_frames="); Serial1.println(s->tx_frames);
          Serial1.print("rx_dropped="); Serial1.println(s->rx_dropped);
          Serial1.print("link_changes="); Serial1.println(s->link_changes);
        }
        Serial1.print("> ");
        line = "";
      }
    } else { line += c; }
  }
  yield();
}
