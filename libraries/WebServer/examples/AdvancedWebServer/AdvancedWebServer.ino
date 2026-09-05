/*
   AdvancedWebServer

   A page that refreshes itself, an SVG graph drawn on the fly, and a form
   that posts back -- the three things a real device web page usually needs.

   Derived from the AdvancedWebServer example in the ESP8266 core and
   arduino-pico, by Ivan Grokhotkov and Earle F. Philhower III, and licensed
   as that library is: LGPL-2.1-or-later.

   ---
   For the CH32H41x core, over Ethernet. The analog trace is this part's ADC,
   which is 12-bit -- 0..4095, not 0..1023.
*/

#include <LwipEthernet.h>
#include <WebServer.h>

WebServer server(80);

static uint32_t requests = 0;

void handleRoot() {
  requests++;
  unsigned long sec = millis() / 1000;
  unsigned long min = sec / 60;
  unsigned long hr = min / 60;

  char temp[900];
  snprintf(temp, sizeof(temp),
           "<html>\
<head><meta http-equiv='refresh' content='5'/><title>CH32H41x</title>\
<style>body{background:#101418;color:#dfe6ec;font-family:system-ui,sans-serif;margin:2rem}\
h1{font-weight:600}a{color:#7fd1e6}</style></head>\
<body>\
<h1>Hello from a CH32H417</h1>\
<p>Uptime: %02lu:%02lu:%02lu</p>\
<p>Requests served: %lu</p>\
<p>Free heap: %u bytes</p>\
<p><img src='/graph.svg' /></p>\
<p><a href='/form'>A form</a></p>\
</body></html>",
           hr, min % 60, sec % 60, (unsigned long)requests,
           (unsigned)CH32H4.getFreeHeap());
  server.send(200, "text/html", temp);
}

/* An SVG drawn from live readings. No JavaScript, no files, nothing to
   upload -- which is why this trick is worth knowing on a part with no
   filesystem configured. */
void drawGraph() {
  String out;
  out.reserve(3000);
  out += "<svg xmlns='http://www.w3.org/2000/svg' width='400' height='150'>\n";
  out += "<rect width='400' height='150' fill='#182028'/>\n";
  out += "<g stroke='#7fd1e6' stroke-width='2' fill='none'><path d='M0 ";
  int y = 150 - (analogRead(A0) * 150 / 4095);
  out += String(y);
  for (int x = 4; x < 400; x += 4) {
    y = 150 - (analogRead(A0) * 150 / 4095);
    out += " L" + String(x) + " " + String(y);
  }
  out += "'/></g>\n</svg>\n";
  server.send(200, "image/svg+xml", out);
}

void handleForm() {
  if (server.method() == HTTP_POST) {
    String msg = "You said: " + server.arg("msg");
    server.send(200, "text/plain", msg);
    return;
  }
  server.send(200, "text/html",
              "<form method='POST' action='/form'>"
              "<input name='msg'><input type='submit'></form>");
}

void handleNotFound() {
  server.send(404, "text/plain", "File Not Found\n\nURI: " + server.uri());
}

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

  server.on("/", handleRoot);
  server.on("/graph.svg", drawGraph);
  server.on("/form", handleForm);
  server.onNotFound(handleNotFound);
  server.begin();
  Serial.println("HTTP server started");
}

void loop() {
  server.handleClient();
}
