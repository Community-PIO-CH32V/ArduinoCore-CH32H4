/* mDNS, on Serial1 and without waiting for anything.

   Deliberately not the example: an example that blocks on `while (!Serial)`
   cannot be debugged, because the thing under test never starts until a host
   opens a port that only appears once the thing has started.
*/
#include <Arduino.h>
#include <LwipEthernet.h>
#include <MDNS.h>

extern "C" {
#include "lwip/netif.h"
#include "lwip/igmp.h"
#include "lwip/apps/mdns.h"
}

static bool mdnsUp = false;
static int httpSlot = -1;

void setup() {
  Serial1.begin(115200);
  Serial1.println("mdnstest booting");

  const int rc = Ethernet.begin();
  Serial1.print("eth_begin="); Serial1.println(rc);
  Serial1.print("ip="); Serial1.println(Ethernet.localIP());

  mdnsUp = MDNS.begin("ch32h4");
  Serial1.print("mdns_begin="); Serial1.println(mdnsUp ? 1 : 0);
  if (mdnsUp) {
    httpSlot = MDNS.addService("http", "tcp", 80);
    Serial1.print("mdns_http_slot="); Serial1.println(httpSlot);
  }
  Serial1.println("mdnstest ready");
  Serial1.print("> ");
}

void loop() {
  static String line;
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        if (line == "info") {
          struct netif *n = netif_default;
          Serial1.print("ip="); Serial1.println(Ethernet.localIP());
          Serial1.print("mdns_running="); Serial1.println(MDNS.running() ? 1 : 0);
          Serial1.print("netif_default="); Serial1.println(n ? 1 : 0);
          if (n) {
            Serial1.print("netif_up="); Serial1.println(netif_is_up(n) ? 1 : 0);
            Serial1.print("netif_link="); Serial1.println(netif_is_link_up(n) ? 1 : 0);
            Serial1.print("netif_igmp="); Serial1.println((n->flags & NETIF_FLAG_IGMP) ? 1 : 0);
            Serial1.print("netif_bcast="); Serial1.println((n->flags & NETIF_FLAG_BROADCAST) ? 1 : 0);
            Serial1.print("netif_name="); Serial1.print(n->name[0]); Serial1.println(n->name[1]);
          }
        } else if (line == "filt") {
          Serial1.print("macffr=0x"); Serial1.println(ETH->MACFFR, HEX);
          Serial1.print("machthr=0x"); Serial1.println(ETH->MACHTHR, HEX);
          Serial1.print("machtlr=0x"); Serial1.println(ETH->MACHTLR, HEX);
          Serial1.print("maca0hr=0x"); Serial1.println(ETH->MACA0HR, HEX);
          Serial1.print("maca0lr=0x"); Serial1.println(ETH->MACA0LR, HEX);

        } else if (line.startsWith("hashbin ")) {
          const int bin = line.substring(8).toInt();
          ETH->MACHTHR = (bin >= 32) ? (1u << (bin - 32)) : 0u;
          ETH->MACHTLR = (bin < 32) ? (1u << bin) : 0u;
          Serial1.print("bin="); Serial1.println(bin);
          Serial1.print("machthr=0x"); Serial1.println(ETH->MACHTHR, HEX);
          Serial1.print("machtlr=0x"); Serial1.println(ETH->MACHTLR, HEX);

        } else if (line == "hashall") {
          /* Every bin open. If mDNS answers now, the bin index was wrong;
             if it still does not, the hash filter is not the problem. */
          ETH->MACHTHR = 0xFFFFFFFFu;
          ETH->MACHTLR = 0xFFFFFFFFu;
          Serial1.println("hashall=1");

        } else if (line == "promisc") {
          /* Accept everything. The last word on whether reception is the
             problem at all. */
          ETH->MACFFR |= 0x00000001u;
          Serial1.print("macffr=0x"); Serial1.println(ETH->MACFFR, HEX);

        } else if (line == "announce") {
          if (netif_default) {
            mdns_resp_announce(netif_default);
            Serial1.println("announced=1");
          } else {
            Serial1.println("announced=0");
          }
        } else if (line == "restart") {
          MDNS.end();
          mdnsUp = MDNS.begin("ch32h4");
          Serial1.print("mdns_begin="); Serial1.println(mdnsUp ? 1 : 0);
          httpSlot = MDNS.addService("http", "tcp", 80);
          Serial1.print("mdns_http_slot="); Serial1.println(httpSlot);
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
