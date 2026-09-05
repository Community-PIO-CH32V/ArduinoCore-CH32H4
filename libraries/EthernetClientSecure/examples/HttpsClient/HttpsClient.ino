/*
   A TLS client, with the certificate actually checked.

   The shape is WiFiClientSecure's, from the ESP cores, because that is what
   sketches are written against: make a client, give it a root certificate,
   connect.

   This example code is in the public domain.

   ---
   For the CH32H41x core. TLS 1.2 and 1.3 through mbedTLS, with AES on the
   part's ECDC accelerator and entropy from its hardware TRNG.

   THE CLOCK HAS TO BE RIGHT, and this is the first thing that goes wrong.
   Certificate validity is checked against the RTC, and a board that has just
   powered on thinks it is the year 2000 -- so every certificate ever issued
   reads as "not yet valid" and every connection fails. verifyErrorString()
   says so in as many words when that is what happened, which is why it is
   printed below rather than just a number.
*/

#include <LwipEthernet.h>
#include <NTP.h>
#include <EthernetClientSecure.h>

extern "C" {
#include "ch32h4_rtc.h"
}

/* Whatever your server's chain ends at. This is a placeholder. */
static const char root_ca[] =
  "-----BEGIN CERTIFICATE-----\n"
  "...paste the root certificate here...\n"
  "-----END CERTIFICATE-----\n";

static const char *host = "example.com";

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

  /* The RTC does not start itself. */
  if (!ch32h4_rtc_begin(CH32H4_RTC_SRC_LSE)) {
    ch32h4_rtc_begin(CH32H4_RTC_SRC_LSI);
  }
  NTP.begin("pool.ntp.org");
  if (!NTP.waitSynced(20000)) {
    Serial.println("WARNING: clock not set, certificates will look invalid");
  }

  EthernetClientSecure client;
  client.setCACert(root_ca);

  Serial.print("connecting to ");
  Serial.println(host);

  if (!client.connect(host, 443)) {
    Serial.print("connect failed: ");
    Serial.println(client.lastErrorString());
    Serial.print("certificate:    ");
    Serial.println(client.verifyErrorString());
    return;
  }

  Serial.println("connected");

  client.print("GET / HTTP/1.1\r\nHost: ");
  client.print(host);
  client.print("\r\nConnection: close\r\n\r\n");

  const uint32_t deadline = millis() + 10000;
  while (client.connected() && millis() < deadline) {
    while (client.available()) {
      Serial.write(client.read());
    }
    yield();
  }
  client.stop();
  Serial.println("\n-- done --");
}

void loop() {
  delay(1000);
}
