/*
   DHCP-based IP printer

   This sketch uses the DHCP extensions to the Ethernet library
   to get an IP address via DHCP and print the address obtained.
   using an Arduino Wiznet Ethernet shield.

   created 12 April 2011
   modified 9 Apr 2012
   by Tom Igoe
   modified 02 Sept 2015
   by Arturo Guadalupi

   This example code is in the public domain.

   ---
   For the CH32H41x core, whose Ethernet is on-chip rather than a shield --
   there is no MAC address to make up and no SPI bus to share. The MAC comes
   from the part's unique ID, so two boards on one network do not collide.

   Ethernet.begin() blocks for up to fifteen seconds waiting for a lease.
   Pass 0 to have it return as soon as the interface is up and let DHCP finish
   in the background, which is what a sketch with something else to do wants.
*/

#include <LwipEthernet.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.println("Starting Ethernet...");

  if (Ethernet.begin() == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    if (Ethernet.hardwareStatus() == LwipEthernetClass::NoHardware) {
      Serial.println("Ethernet hardware was not found.");
    } else if (Ethernet.linkStatus() == LwipEthernetClass::LinkOFF) {
      Serial.println("Ethernet cable is not connected.");
    }
    return;
  }

  uint8_t mac[6];
  Ethernet.macAddress(mac);
  Serial.print("MAC address: ");
  for (int i = 0; i < 6; i++) {
    if (mac[i] < 16) {
      Serial.print("0");
    }
    Serial.print(mac[i], HEX);
    if (i < 5) {
      Serial.print(":");
    }
  }
  Serial.println();

  Serial.print("IP address:  ");
  Serial.println(Ethernet.localIP());
  Serial.print("Subnet mask: ");
  Serial.println(Ethernet.subnetMask());
  Serial.print("Gateway:     ");
  Serial.println(Ethernet.gatewayIP());
  Serial.print("DNS server:  ");
  Serial.println(Ethernet.dnsIP());
}

void loop() {
  delay(1000);
}
