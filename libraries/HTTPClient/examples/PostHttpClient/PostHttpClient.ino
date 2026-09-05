/*
   PostHttpClient.ino

   Sends a POST body and reads the reply, and shows how to set a header and
   read one back.

   Derived from the PostHttpClient example in the ESP8266 core and
   arduino-pico, and licensed as the HTTPClient library is:
   LGPL-2.1-or-later.

   ---
   For the CH32H41x core, over Ethernet. collectHeaders() before GET()/POST()
   is what makes header() return anything: the parser discards headers it was
   not asked to keep, because on a part with this much RAM keeping all of them
   is not free.
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

  if (http.begin(client, "http://httpbin.org/post")) {
    http.addHeader("Content-Type", "application/json");

    const char *keys[] = { "Content-Type", "Server" };
    http.collectHeaders(keys, 2);

    int code = http.POST("{\"board\":\"ch32h417\",\"hello\":true}");

    if (code > 0) {
      Serial.printf("[HTTP] POST code: %d\n", code);
      Serial.print("Content-Type: ");
      Serial.println(http.header("Content-Type"));
      Serial.print("Server: ");
      Serial.println(http.header("Server"));
      if (code == HTTP_CODE_OK) {
        Serial.println(http.getString());
      }
    } else {
      Serial.printf("[HTTP] POST failed: %s\n",
                    http.errorToString(code).c_str());
    }
    http.end();
  }

  delay(15000);
}
