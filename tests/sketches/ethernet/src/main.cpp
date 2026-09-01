/* The Ethernet acceptance sketch: DHCP, TCP in both directions, and UDP.
 *
 * Everything here is driven from the console on Serial1 so the host-side test
 * can ask for one thing at a time. The network side is deliberately exercised
 * against a real peer rather than against the board itself -- a loopback test
 * proves the API compiles, not that a frame ever reached the wire.
 */
#include <Arduino.h>
#include <LwipEthernet.h>
extern "C" {
#include "ch32h4_rtc.h"
}

EthernetServer echoServer(7000);
EthernetUDP udp;

static char line[96];
static int len = 0;

void setup() {
  Serial1.begin(115200);
  Serial1.println("ethtest starting");

  Ethernet.setHostname("ch32h417-arduino");

  int ok = Ethernet.begin(15000);
  Serial1.print("dhcp="); Serial1.println(ok);
  Serial1.print("hw="); Serial1.println((int)Ethernet.hardwareStatus());
  Serial1.print("link="); Serial1.println((int)Ethernet.linkStatus());
  Serial1.print("ip="); Serial1.println(Ethernet.localIP());
  Serial1.print("gw="); Serial1.println(Ethernet.gatewayIP());
  Serial1.print("mask="); Serial1.println(Ethernet.subnetMask());
  Serial1.print("dns="); Serial1.println(Ethernet.dnsIP());

  uint8_t mac[6];
  Ethernet.macAddress(mac);
  Serial1.print("mac=");
  for (int i = 0; i < 6; i++) {
    if (i) Serial1.print(":");
    if (mac[i] < 16) Serial1.print("0");
    Serial1.print(mac[i], HEX);
  }
  Serial1.println();

  echoServer.setNoDelay(true);
  echoServer.begin();
  Serial1.print("tcp_server="); Serial1.println((bool)echoServer ? 1 : 0);

  Serial1.print("udp_begin="); Serial1.println(udp.begin(7001));

  Serial1.println("ethtest ready");
  Serial1.print("> ");
}

/* Neither of these pumps prints a prompt. A prompt is the reply to a command
   being complete, and an unsolicited one makes the host-side reader return
   early with half an answer -- which reads as the board ignoring the command.

   The echo server runs continuously, not on command: the host connects
   whenever it likes, and a server that only ran inside a command handler
   would prove nothing about the receive path between commands. */
static void pumpEchoServer() {
  static EthernetClient client;
  if (!client) {
    client = echoServer.accept();
    if (client) {
      Serial1.print("tcp_accept from ");
      Serial1.print(client.remoteIP());
      Serial1.print(":");
      Serial1.println(client.remotePort());
    }
  }
  if (client) {
    uint8_t buf[256];
    int n = client.available();
    if (n > 0) {
      if (n > (int)sizeof(buf)) n = sizeof(buf);
      n = client.read(buf, n);
      if (n > 0) client.write(buf, n);
    } else if (!client.connected()) {
      client.stop();
      Serial1.println("tcp_client_closed");
    }
  }
}

static void pumpUdpEcho() {
  int n = udp.parsePacket();
  if (n <= 0) return;

  uint8_t buf[256];
  IPAddress from = udp.remoteIP();
  uint16_t port = udp.remotePort();
  int got = udp.read(buf, sizeof(buf));

  Serial1.print("udp_rx="); Serial1.print(got);
  Serial1.print(" from "); Serial1.print(from);
  Serial1.print(":"); Serial1.println(port);

  if (got > 0) {
    udp.beginPacket(from, port);
    udp.write(buf, got);
    udp.endPacket();
  }
}

/* `tcpget <a.b.c.d> <port>` -- connect out, send a line, read the answer.
   This is the direction the echo server does not cover. */
static void tcpGet(char *args) {
  char *sp = strchr(args, ' ');
  if (!sp) { Serial1.println("tcp_connect=0 (usage: tcpget ip port)"); return; }
  *sp = '\0';
  IPAddress ip;
  if (!ip.fromString(args)) { Serial1.println("tcp_connect=0 (bad ip)"); return; }
  uint16_t port = (uint16_t)atoi(sp + 1);

  EthernetClient c;
  c.setTimeout(5000);
  int ok = c.connect(ip, port);
  Serial1.print("tcp_connect="); Serial1.println(ok);
  if (!ok) return;

  Serial1.print("tcp_local_port="); Serial1.println(c.localPort());
  c.print("hello from ch32h417\n");
  c.flush();

  uint32_t start = millis();
  String reply;
  while (millis() - start < 3000 && reply.length() < 200) {
    while (c.available()) {
      char ch = (char)c.read();
      if (ch == '\n') { start = 0; break; }
      reply += ch;
    }
    if (start == 0) break;
    if (!c.connected() && !c.available()) break;
    yield();
  }
  Serial1.print("tcp_reply="); Serial1.println(reply);
  c.stop();
  Serial1.print("tcp_closed="); Serial1.println(c.connected() ? 0 : 1);
}

static void udpSend(char *args) {
  char *sp = strchr(args, ' ');
  if (!sp) { Serial1.println("udp_send=0"); return; }
  *sp = '\0';
  IPAddress ip;
  if (!ip.fromString(args)) { Serial1.println("udp_send=0 (bad ip)"); return; }
  uint16_t port = (uint16_t)atoi(sp + 1);

  udp.beginPacket(ip, port);
  udp.print("hello udp from ch32h417");
  int ok = udp.endPacket();
  Serial1.print("udp_send="); Serial1.println(ok);
}

static void handle(char *cmd) {
  if (!strcmp(cmd, "netstat")) {
    Serial1.print("status="); Serial1.println(Ethernet.status());
    Serial1.print("link="); Serial1.println((int)Ethernet.linkStatus());
    Serial1.print("ip="); Serial1.println(Ethernet.localIP());
    const eth_stats_t *s = eth_get_stats(&eth_instance);
    Serial1.print("rx_frames="); Serial1.println(s->rx_frames);
    Serial1.print("tx_frames="); Serial1.println(s->tx_frames);
    Serial1.print("rx_dropped="); Serial1.println(s->rx_dropped);
    Serial1.print("rx_buf_unavail="); Serial1.println(s->rx_buf_unavail);
    Serial1.print("tx_errors="); Serial1.println(s->tx_errors);
    Serial1.print("link_changes="); Serial1.println(s->link_changes);

  } else if (!strncmp(cmd, "tcpget ", 7)) {
    tcpGet(cmd + 7);

  } else if (!strncmp(cmd, "udpsend ", 8)) {
    udpSend(cmd + 8);

  } else if (!strncmp(cmd, "ntpsync", 7)) {
    /* ntpsync [server] -- default the DHCP-offered server, then pool.ntp.org.
       Deliberately starts the RTC from an UNSET state each time, so a pass
       cannot come from a clock that was already right. */
    const char *server = (cmd[7] == ' ') ? cmd + 8 : nullptr;

    /* Clear the clock first, by selecting a different source and coming back:
       changing RTCSEL resets the backup domain, which resets the counter. A
       sync test against a clock that is already right proves nothing -- it
       passes in three milliseconds without a datagram leaving the board. */
    ch32h4_rtc_begin(CH32H4_RTC_SRC_LSI);
    Serial1.print("rtc_begin=");
    Serial1.println(ch32h4_rtc_begin(CH32H4_RTC_SRC_LSE) ? 1 : 0);
    Serial1.print("rtc_was_set="); Serial1.println(ch32h4_rtc_is_set() ? 1 : 0);

    uint32_t t0 = millis();
    bool started = NTP.begin(server);
    Serial1.print("ntp_begin="); Serial1.println(started ? 1 : 0);
    if (!started) { Serial1.print("> "); return; }

    bool ok = NTP.waitAnswer(20000);
    Serial1.print("ntp_answer="); Serial1.println(ok ? 1 : 0);
    Serial1.print("ntp_synced="); Serial1.println(NTP.synced() ? 1 : 0);
    Serial1.print("ntp_ms="); Serial1.println(millis() - t0);
    if (ok) {
      struct timeval tv = {};
      gettimeofday(&tv, nullptr);
      Serial1.print("ntp_unix="); Serial1.println((uint32_t)tv.tv_sec);
      struct tm tmv;
      time_t now = tv.tv_sec;
      if (gmtime_r(&now, &tmv)) {
        char buf[32];
        strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", &tmv);
        Serial1.print("ntp_iso="); Serial1.println(buf);
      }
    }

  } else if (!strcmp(cmd, "ntpstat")) {
    Serial1.print("ntp_running="); Serial1.println(NTP.running() ? 1 : 0);
    Serial1.print("ntp_synced="); Serial1.println(NTP.synced() ? 1 : 0);
    Serial1.print("ntp_last_ms="); Serial1.println(NTP.lastSyncMillis());

  } else if (!strcmp(cmd, "heapinfo")) {
    Serial1.print("heap_free="); Serial1.println((uint32_t)ch32h4_heap_free());
  }
  Serial1.print("> ");
}

void loop() {
  pumpEchoServer();
  pumpUdpEcho();

  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (len) {
        line[len] = '\0';
        handle(line);
        len = 0;
      }
    } else if (len < (int)sizeof(line) - 1) {
      line[len++] = c;
    }
  }
  yield();
}
