/*
   BasicHttpClient.ino

   Created on: 24.05.2015

   Derived from the BasicHttpClient example in the ESP8266 core and
   arduino-pico, and licensed as the HTTPClient library is:
   LGPL-2.1-or-later.

   ---
   For the CH32H41x core: the WiFi setup becomes Ethernet, and the client
   passed to begin() is an EthernetClient.

   THE CLIENT MUST OUTLIVE THE HTTPClient. begin(client, url) stores a pointer
   to it -- the Arduino Client interface has no clone() and cannot grow one --
   so a client declared inside an if block, with the HTTPClient outside it, is
   a dangling pointer that usually still appears to work. ESP32's HTTPClient
   has the same rule for the same reason.
*/

#include <Arduino.h>
#include <LwipEthernet.h>
#include <HTTPClient.h>

EthernetClient client;

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
}

void loop() {
  HTTPClient http;

  Serial.print("[HTTP] begin...\n");
  if (http.begin(client, "http://example.com/")) {
    Serial.print("[HTTP] GET...\n");
    int httpCode = http.GET();

    /* httpCode is the HTTP status when there WAS a response, and a negative
       number when there was not. Collapsing the two is the classic mistake:
       404 is a successful request. */
    if (httpCode > 0) {
      Serial.printf("[HTTP] GET... code: %d\n", httpCode);

      if (httpCode == HTTP_CODE_OK || httpCode == HTTP_CODE_MOVED_PERMANENTLY) {
        String payload = http.getString();
        Serial.println(payload);
      }
    } else {
      Serial.printf("[HTTP] GET... failed, error: %s\n",
                    http.errorToString(httpCode).c_str());
    }

    http.end();
  } else {
    Serial.printf("[HTTP] Unable to connect\n");
  }

  delay(10000);
}
