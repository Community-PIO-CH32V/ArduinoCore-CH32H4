/*
   HelloServerSecure

   The same one-route server, over HTTPS.

   Derived from HelloServerBearSSL in arduino-pico, by Earle F. Philhower III,
   LGPL-2.1-or-later -- though the TLS underneath is mbedTLS here, not
   BearSSL, so the certificate is handed over differently.

   ---
   For the CH32H41x core. Two things differ from the plain server:

     - including <WebServerSecure.h> is what pulls mbedTLS into the build.
       There is no flag to set; the #include is the switch.
     - the certificate and key go on the server object, not the WebServer,
       because a WebServer constructor has nowhere to put them. getServer()
       is the EthernetServerSecure underneath.

   THE CERTIFICATE BELOW IS A PLACEHOLDER and will not work. Make your own --
   it takes a second, and a self-signed certificate is fine on a LAN:

       openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 \
           -nodes -days 3650 -keyout key.pem -out cert.pem \
           -subj "/CN=192.168.1.50" \
           -addext "subjectAltName=IP:192.168.1.50"

   Put the board's own address in the SAN: browsers stopped looking at CN
   years ago, and a certificate without a SAN is rejected outright rather than
   warned about. Every browser will still warn that it does not know your CA;
   that warning is accurate.

   AND KNOW WHAT IT COSTS. The handshake runs inside handleClient() and blocks
   until it finishes, and each live connection holds an mbedTLS context of
   roughly 20 KB. A handful of small pages is comfortable. A site is not.
*/

#include <LwipEthernet.h>
#include <WebServerSecure.h>

WebServerSecure server(443);

static const char server_cert[] =
  "-----BEGIN CERTIFICATE-----\n"
  "...your certificate here...\n"
  "-----END CERTIFICATE-----\n";

static const char server_key[] =
  "-----BEGIN PRIVATE KEY-----\n"
  "...your key here...\n"
  "-----END PRIVATE KEY-----\n";

void handleRoot() {
  server.send(200, "text/plain", "hello over TLS\r\n");
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
  Serial.print("https://");
  Serial.println(Ethernet.localIP());

  server.getServer().setCertificate(server_cert);
  server.getServer().setPrivateKey(server_key);

  server.on("/", handleRoot);
  server.begin();
  Serial.println("HTTPS server started");
}

void loop() {
  server.handleClient();

  /* Why the last handshake failed, if one did. With a placeholder certificate
     this is where "the key does not match" shows up. */
  static int lastErr = 0;
  int err = server.getServer().lastHandshakeError();
  if (err != lastErr) {
    lastErr = err;
    if (err != 0) {
      Serial.print("handshake error: ");
      Serial.println(server.getServer().lastHandshakeErrorString());
    }
  }
}
