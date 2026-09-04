/* WebServer and HTTPClient, compiled and exercised over Ethernet.

   Both libraries came from arduino-pico with WiFiClient replaced by the
   Client interface they were already restricted to. This sketch is what says
   the substitution held: it serves requests through WebServer and makes them
   through HTTPClient, against itself.
*/
#include <Arduino.h>
#include <LwipEthernet.h>
#include <WebServer.h>
#include <HTTPClient.h>

static WebServer server(80);
static volatile uint32_t s_hits = 0;

static void handleRoot() {
  s_hits++;
  server.send(200, "text/plain", "hello from ch32h4");
}

static void handleEcho() {
  /* Query-string parsing, which is the part of the port most likely to have
     been broken by the type substitution: it reads from the client through
     the Stream interface rather than the concrete class. */
  s_hits++;
  server.send(200, "text/plain", server.arg("v"));
}

static void handleNotFound() {
  server.send(404, "text/plain", "nope");
}

void setup() {
  Serial1.begin(115200);

  Ethernet.begin();
  const uint32_t deadline = millis() + 20000;
  while (!Ethernet.connected() && millis() < deadline) {
    delay(100);
  }

  server.on("/", handleRoot);
  server.on("/echo", handleEcho);
  server.onNotFound(handleNotFound);
  server.begin();

  Serial1.print("web_ip=");
  Serial1.println(Ethernet.localIP());
  Serial1.print("web_up=");
  Serial1.println(Ethernet.connected() ? 1 : 0);
  Serial1.println("webserver ready");
  Serial1.print("> ");
}

void loop() {
  server.handleClient();

  static String line;
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        if (line == "webinfo") {
          Serial1.print("web_ip=");
          Serial1.println(Ethernet.localIP());
          Serial1.print("web_up=");
          Serial1.println(Ethernet.connected() ? 1 : 0);
          Serial1.print("web_hits=");
          Serial1.println((int)s_hits);

        } else if (line.startsWith("webget ")) {
          /* HTTPClient against our own WebServer. One sketch, both libraries,
             a real socket between them -- which is the only way to find out
             whether the two ports agree about what an HTTP response is. */
          const String path = line.substring(7);
          EthernetClient client;
          HTTPClient http;
          String url = "http://" + Ethernet.localIP().toString() + path;
          if (!http.begin(client, url)) {
            Serial1.println("web_get_rc=-1000");
          } else {
            const int rc = http.GET();
            Serial1.print("web_get_rc=");
            Serial1.println(rc);
            if (rc > 0) {
              const String body = http.getString();
              Serial1.print("web_get_len=");
              Serial1.println((int)body.length());
              Serial1.print("web_get_body=");
              Serial1.println(body);
            }
            http.end();
          }

        } else {
          Serial1.println("?");
        }
      }
      line = "";
      Serial1.print("> ");
    } else {
      line += c;
    }
  }
}
