/*
   BasicHttpsClient.ino

   Fetches an HTTPS page, with the certificate actually verified.

   Derived from the BasicHttpsClient example in the ESP8266 core and
   arduino-pico, and licensed as the HTTPClient library is:
   LGPL-2.1-or-later.

   ---
   For the CH32H41x core. Two things that are specific to it:

   HTTPClientSecure, NOT HTTPClient. The HTTPS setters live on a subclass in
   its own header, because including that header is what pulls mbedTLS into
   the build -- so a sketch speaking plain HTTP never pays for it. ESP32 puts
   these methods on HTTPClient itself; it can, because its SDK always has
   mbedTLS in it.

   THE CLOCK HAS TO BE RIGHT. Certificate validity is checked against the RTC,
   and a board that has just powered on thinks it is the year 2000 -- so every
   certificate ever issued reads as "not yet valid" and every connection
   fails, correctly and confusingly. Sync first, as below.

   setInsecure() exists and is honestly named. It skips verification
   altogether: the connection is encrypted against a passive listener and
   offers nothing at all against anyone who can answer for the server. Fine
   for bringing a board up, not for shipping.
*/

#include <Arduino.h>
#include <LwipEthernet.h>
#include <NTP.h>
#include <HTTPClientSecure.h>

extern "C" {
#include "ch32h4_rtc.h"
}

/* ISRG Root X1 -- Let's Encrypt's root, which most of the web chains to.
   Replace with whatever your server actually chains to. */
static const char root_ca[] =
  "-----BEGIN CERTIFICATE-----\n"
  "...paste the root certificate here...\n"
  "-----END CERTIFICATE-----\n";

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

  if (!ch32h4_rtc_begin(CH32H4_RTC_SRC_LSE)) {
    ch32h4_rtc_begin(CH32H4_RTC_SRC_LSI);
  }
  NTP.begin("pool.ntp.org");
  if (!NTP.waitSynced(20000)) {
    Serial.println("clock not set -- every certificate will look invalid");
  }
}

void loop() {
  HTTPClientSecure https;

  https.setCACert(root_ca);

  if (https.begin("https://example.com/")) {
    int code = https.GET();
    if (code > 0) {
      Serial.printf("[HTTPS] GET code: %d\n", code);
      if (code == HTTP_CODE_OK) {
        Serial.println(https.getString());
      }
    } else {
      Serial.printf("[HTTPS] GET failed: %s\n",
                    https.errorToString(code).c_str());
    }
    https.end();
  } else {
    Serial.println("[HTTPS] unable to connect");
  }

  delay(15000);
}
