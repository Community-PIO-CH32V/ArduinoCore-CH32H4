/*
   HelloServer

   The smallest useful WebServer sketch: one route, a 404 handler, and
   handleClient() in loop().

   Derived from the HelloServer example in the ESP8266 core and arduino-pico,
   by Ivan Grokhotkov and Earle F. Philhower III, LGPL-2.1-or-later, as the
   WebServer library itself is.

   ---
   For the CH32H41x core. The WiFi setup of the original is Ethernet here.

   THE LED NEEDS A JUMPER. LED_BUILTIN is LED1, which sits behind a header pin
   under PE2 and is not wired to the microcontroller until that pin is
   jumpered. The sketch works either way; without the jumper the LED simply
   does not light.

   handleClient() must be called often. It is where a request is read, routed
   and answered -- a loop() that blocks for a second answers requests once a
   second.
*/

#include <LwipEthernet.h>
#include <WebServer.h>

WebServer server(80);

static uint32_t hits = 0;

void handleRoot() {
  digitalWrite(LED_BUILTIN, HIGH);
  hits++;
  server.send(200, "text/plain", "hello from ch32h4!\r\n");
  digitalWrite(LED_BUILTIN, LOW);
}

void handleNotFound() {
  String message = "File Not Found\n\n";
  message += "URI: ";
  message += server.uri();
  message += "\nMethod: ";
  message += (server.method() == HTTP_GET) ? "GET" : "POST";
  message += "\nArguments: ";
  message += server.args();
  message += "\n";
  for (uint8_t i = 0; i < server.args(); i++) {
    message += " " + server.argName(i) + ": " + server.arg(i) + "\n";
  }
  server.send(404, "text/plain", message);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  pinMode(LED_BUILTIN, OUTPUT);
  digitalWrite(LED_BUILTIN, LOW);

  if (Ethernet.begin() == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    return;
  }
  Serial.print("IP address: ");
  Serial.println(Ethernet.localIP());

  server.on("/", handleRoot);

  server.on("/inline", []() {
    server.send(200, "text/plain", "this works as well");
  });

  server.onNotFound(handleNotFound);

  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
}
