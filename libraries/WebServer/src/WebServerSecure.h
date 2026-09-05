/* HTTPS: the same WebServer, over a server-side TLS connection.
 *
 *     #include <WebServerSecure.h>
 *
 *     WebServerSecure server;      // port 443
 *
 *     void setup() {
 *         Ethernet.begin();
 *         server.getServer().setCertificate(server_cert_pem);
 *         server.getServer().setPrivateKey(server_key_pem);
 *         server.on("/", []{ server.send(200, "text/plain", "hi"); });
 *         server.begin();
 *     }
 *     void loop() { server.handleClient(); }
 *
 * THE CERTIFICATE AND KEY ARE NOT OPTIONAL, and there is nowhere sensible to
 * pass them to the WebServer constructor, so they are set on the server it
 * wraps -- getServer() returns the EthernetServerSecure. Without them every
 * accept() drops the connection and lastHandshakeError() says why.
 *
 * A SELF-SIGNED CERTIFICATE IS FINE ON A LAN and is what every browser will
 * warn about; that warning is accurate, and clicking through it is the user
 * deciding to trust the board on sight. Generating one:
 *
 *     openssl req -x509 -newkey rsa:2048 -nodes -days 3650 \
 *         -keyout key.pem -out cert.pem -subj "/CN=192.168.1.50" \
 *         -addext "subjectAltName=IP:192.168.1.50"
 *
 * The SAN matters: browsers have not looked at CN for years, and a
 * certificate without one is rejected outright rather than warned about.
 *
 * WHAT THIS COSTS. Read the header comment in EthernetServerSecure.h before
 * building anything on this: the handshake blocks inside handleClient(), and
 * each live connection holds an mbedTLS context on the order of 20 KB. A
 * browser opens several connections to one page. Serving a handful of small
 * pages is comfortable; serving a site is not what this part is.
 *
 * This is a separate header from WebServer.h on purpose. Including it is what
 * pulls in mbedTLS -- a quarter of a megabyte of flash -- so a sketch that
 * only wants HTTP never pays for it.
 *
 * It needs board_build.tls = mbedtls, the same setting EthernetClientSecure
 * uses -- one mbedTLS build serves both directions.
 */
#pragma once

#include <EthernetServerSecure.h>

#include "WebServerTemplate.h"
#include "detail/mimetable.h"

using WebServerSecure = WebServerTemplate<EthernetServerSecure, 443>;
