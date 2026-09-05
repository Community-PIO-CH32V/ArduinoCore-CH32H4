/* WebServerSecure: HTTPS, served by the board.

   The counterpart to tlstest, which is the TLS client. This is the server:
   EthernetServerSecure runs the handshake on every accepted socket and
   WebServerSecure routes what comes out of it, so the same handlers a plain
   WebServer would have serve over TLS instead.

   IT DOES NOT TEST ITSELF, and cannot. A self-connection would deadlock: the
   server's handshake runs inside handleClient(), and a client handshake in the
   same loop() blocks before handleClient() is reached. tests/hw/test_tls_server.py
   drives it from the PC with Python's ssl module, which is the better test
   anyway -- interoperating with an independent TLS implementation is what
   says the handshake is right, and agreeing with ourselves says nothing.

   THE CLOCK IS A COMMAND, NOT PART OF setup(). Serving HTTPS needs no clock at
   all -- the client checks the server's certificate against the CLIENT's
   clock. Mutual TLS does, because then the board is the one verifying, and a
   board that thinks it is 2020 finds every certificate ever issued "not yet
   valid". Both states are reachable from here on purpose: `clock behind` and
   `clock sync`, so the test can check that the failure says what is wrong
   rather than only that the success works.

   The certificates are in certs.h, generated from certs/ by
   certs/make_certs.sh. Self-signed, EC P-256, CN and SAN ch32h4.local.
*/
#include <Arduino.h>
#include <LwipEthernet.h>
#include <NTP.h>
#include <WebServerSecure.h>

#include "certs.h"

extern "C" {
#include "ch32h4_rtc.h"
}

static WebServerSecure server(443);

static volatile uint32_t s_hits = 0;

/* 2020-01-01. Deliberately a real time rather than the power-on value: it is
   below ch32h4_rtc_is_set()'s 2024 threshold, so the clock still reads as
   unset -- which is the state a board with no battery boots into, and the one
   whose error message is worth checking. */
static const time_t CLOCK_BEHIND = 1577836800;

/* A body longer than MBEDTLS_SSL_OUT_CONTENT_LEN (2048), so the multi-record
   path is exercised rather than assumed. mbedtls fragments application data
   across records on its own; the reason to check is that our write() loop has
   to keep feeding it after a partial write, and a body that fits in one record
   never finds out whether it does. */
static String bigBody() {
    String s;
    s.reserve(5000);
    for (int i = 0; i < 500; i++) {
        s += "0123456789";
    }
    return s;
}

static void handleRoot() {
    s_hits++;
    server.send(200, "text/plain", "hello over tls");
}

static void handleEcho() {
    s_hits++;
    server.send(200, "text/plain", server.arg("v"));
}

static void handleBig() {
    s_hits++;
    server.send(200, "text/plain", bigBody());
}

static void handleWho() {
    /* Which client this is, from the TLS layer rather than the socket. Proves
       WebServerTemplate::client() coerces back to the concrete type -- through
       a Client* none of this is reachable. */
    s_hits++;
    server.send(200, "text/plain", server.client().remoteIP().toString());
}

static void handleNotFound() {
    server.send(404, "text/plain", "nope");
}

static void reportClock() {
    Serial1.print("tls_rtcrun=");
    Serial1.println(ch32h4_rtc_running() ? 1 : 0);
    Serial1.print("tls_rtc=");
    Serial1.println(ch32h4_rtc_is_set() ? 1 : 0);
    Serial1.print("tls_time=");
    Serial1.println((long)time(nullptr));
}

void setup() {
    Serial1.begin(115200);

    Ethernet.begin();
    const uint32_t deadline = millis() + 20000;
    while (!Ethernet.connected() && millis() < deadline) {
        delay(100);
    }

    /* The RTC does not start itself: nothing in the core turns it on, because
       most sketches have no use for a clock and the backup domain costs a few
       microamps. Mutual TLS is exactly the case that needs it -- see the
       header -- so it is started here, and reportClock() says whether it took.
       LSE first, LSI if the board has no 32 kHz crystal. */
    if (!ch32h4_rtc_begin(CH32H4_RTC_SRC_LSE)) {
        ch32h4_rtc_begin(CH32H4_RTC_SRC_LSI);
    }

    server.getServer().setCertificate(server_cert_pem);
    server.getServer().setPrivateKey(server_key_pem);
    server.getServer().setClientCACert(client_ca_pem);
    /* Off by default. "mtls on" turns it on, so both answers are testable in
       one flash. */
    server.getServer().requireClientCert(false);

    server.on("/", handleRoot);
    server.on("/echo", handleEcho);
    server.on("/big", handleBig);
    server.on("/who", handleWho);
    server.onNotFound(handleNotFound);
    server.begin();

    Serial1.print("tls_ip=");
    Serial1.println(Ethernet.localIP());
    Serial1.print("tls_up=");
    Serial1.println(Ethernet.connected() ? 1 : 0);
    reportClock();
    Serial1.println("tlsserver ready");
    Serial1.print("> ");
}

void loop() {
    server.handleClient();

    static String line;
    while (Serial1.available()) {
        char c = (char)Serial1.read();
        if (c == '\n' || c == '\r') {
            if (line.length()) {
                if (line == "tlsinfo") {
                    Serial1.print("tls_ip=");
                    Serial1.println(Ethernet.localIP());
                    Serial1.print("tls_up=");
                    Serial1.println(Ethernet.connected() ? 1 : 0);
                    Serial1.print("tls_hits=");
                    Serial1.println((int)s_hits);
                    Serial1.print("tls_hserr=");
                    Serial1.println(server.getServer().lastHandshakeError());
                    Serial1.print("tls_hsstr=");
                    Serial1.println(
                        server.getServer().lastHandshakeErrorString());
                    Serial1.print("tls_vferr=");
                    Serial1.println(
                        (unsigned)server.getServer().lastVerifyError());
                    Serial1.print("tls_vfstr=");
                    Serial1.println(
                        server.getServer().lastVerifyErrorString());
                    reportClock();

                } else if (line == "mtls on") {
                    server.getServer().requireClientCert(true);
                    Serial1.println("tls_mtls=1");

                } else if (line == "mtls off") {
                    server.getServer().requireClientCert(false);
                    Serial1.println("tls_mtls=0");

                } else if (line == "clock behind") {
                    ch32h4_rtc_set(CLOCK_BEHIND);
                    reportClock();

                } else if (line == "clock sync") {
                    /* Generous: DNS plus a UDP round trip to a public pool,
                       and the first packet is not retried quickly if it is
                       lost. */
                    NTP.begin("pool.ntp.org");
                    Serial1.print("tls_ntp=");
                    Serial1.println(NTP.waitSynced(25000) ? 1 : 0);
                    reportClock();

                } else if (line == "clock") {
                    reportClock();

                } else if (line == "heap") {
                    Serial1.print("tls_heap=");
                    Serial1.println((int)CH32H4.getFreeHeap());

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
