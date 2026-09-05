/*
   A TLS server, and mutual TLS.

   EthernetServerSecure is EthernetServer with a handshake on every accepted
   socket. Anything that speaks to an EthernetClient speaks to what it hands
   back.

   This example code is in the public domain.

   ---
   For the CH32H41x core. Make a certificate before running this -- a
   self-signed one is fine on a LAN:

       openssl req -x509 -newkey ec -pkeyopt ec_paramgen_curve:prime256v1 \
           -nodes -days 3650 -keyout key.pem -out cert.pem \
           -subj "/CN=192.168.1.50" \
           -addext "subjectAltName=IP:192.168.1.50"

   EC rather than RSA on purpose: the handshake is far cheaper on a part with
   no big-integer accelerator, and MBEDTLS_SSL_OUT_CONTENT_LEN is 2048 here --
   mbedTLS cannot fragment an outgoing handshake message across records, so a
   large RSA chain can be too big to send at all.

   TWO THINGS THAT COST. The handshake runs inside accept() and blocks until
   it finishes -- it calls yield(), so the network stack keeps running, but
   the sketch does not return until it is done. And each live connection holds
   an mbedTLS context of roughly 20 KB against 255 KB of RAM. A handful of
   concurrent connections is the budget on this part, not dozens.

   MUTUAL TLS -- requireClientCert(true) -- is where the board starts checking
   certificates itself, and so where the RTC starts to matter. A clock stuck
   in the past finds every client certificate "not yet valid";
   lastVerifyErrorString() names the clock when that is the cause.
*/

#include <LwipEthernet.h>
#include <NTP.h>
#include <EthernetServerSecure.h>

extern "C" {
#include "ch32h4_rtc.h"
}

static const char server_cert[] =
  "-----BEGIN CERTIFICATE-----\n"
  "...your certificate here...\n"
  "-----END CERTIFICATE-----\n";

static const char server_key[] =
  "-----BEGIN PRIVATE KEY-----\n"
  "...your key here...\n"
  "-----END PRIVATE KEY-----\n";

/* Only needed for mutual TLS: the CA that signs certificates you will accept
   from clients. Leave requireClientCert(false) and this is unused. */
static const char client_ca[] =
  "-----BEGIN CERTIFICATE-----\n"
  "...your client CA here...\n"
  "-----END CERTIFICATE-----\n";

EthernetServerSecure server(443);

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  if (Ethernet.begin() == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    return;
  }

  if (!ch32h4_rtc_begin(CH32H4_RTC_SRC_LSE)) {
    ch32h4_rtc_begin(CH32H4_RTC_SRC_LSI);
  }

  server.setCertificate(server_cert);
  server.setPrivateKey(server_key);

  /* Turn these two on together for mutual TLS. A browser has no client
     certificate and will be refused, which is the point. */
  server.setClientCACert(client_ca);
  server.requireClientCert(false);

  server.begin();

  Serial.print("https://");
  Serial.println(Ethernet.localIP());
}

void loop() {
  EthernetClientSecure client = server.accept();

  if (!client) {
    /* No connection, or a handshake that failed. The error says which. */
    int err = server.lastHandshakeError();
    static int lastErr = 0;
    if (err != 0 && err != lastErr) {
      lastErr = err;
      Serial.print("handshake failed: ");
      Serial.println(server.lastHandshakeErrorString());
      if (server.lastVerifyError() != 0) {
        Serial.print("  client certificate: ");
        Serial.println(server.lastVerifyErrorString());
      }
    }
    return;
  }

  Serial.print("connection from ");
  Serial.println(client.remoteIP());

  /* Read the request line and throw away the rest of the headers. */
  const uint32_t deadline = millis() + 5000;
  while (client.connected() && millis() < deadline) {
    if (client.available()) {
      String line = client.readStringUntil('\n');
      if (line.length() <= 1) {
        break;      /* the blank line that ends the headers */
      }
    }
    yield();
  }

  client.print("HTTP/1.1 200 OK\r\n"
               "Content-Type: text/plain\r\n"
               "Connection: close\r\n"
               "\r\n"
               "hello over TLS\r\n");
  client.flush();
  client.stop();
}
