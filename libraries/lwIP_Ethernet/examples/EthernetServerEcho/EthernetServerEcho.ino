/*
   A TCP echo server.

   Listens on port 2323 and sends back whatever is sent to it. Test it with
   `nc <board-ip> 2323` or PuTTY in raw mode.

   Small, but it covers the three things a server sketch has to get right:
   accepting without blocking, holding more than one client, and noticing when
   one goes away.

   This example code is in the public domain.

   ---
   For the CH32H41x core. accept() hands the connection over and the server
   forgets it, so the sketch owns it from then on -- which is why the clients
   are kept in an array here rather than a single variable. available() is the
   other spelling and keeps the connection in the server's own list; the two
   are not interchangeable.
*/

#include <LwipEthernet.h>

EthernetServer server(2323);

static const int MAX_CLIENTS = 4;
EthernetClient clients[MAX_CLIENTS];

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  if (Ethernet.begin() == 0) {
    Serial.println("Failed to configure Ethernet using DHCP");
    return;
  }

  server.begin();
  Serial.print("echo server on ");
  Serial.print(Ethernet.localIP());
  Serial.println(" port 2323");
}

void loop() {
  /* Take a new connection, if there is one and we have room. */
  EthernetClient incoming = server.accept();
  if (incoming) {
    int slot = -1;
    for (int i = 0; i < MAX_CLIENTS; i++) {
      if (!clients[i] || !clients[i].connected()) {
        slot = i;
        break;
      }
    }
    if (slot < 0) {
      incoming.println("too many clients");
      incoming.stop();
    } else {
      clients[slot] = incoming;
      clients[slot].println("echo server -- type something");
      Serial.print("client ");
      Serial.print(slot);
      Serial.print(" from ");
      Serial.println(clients[slot].remoteIP());
    }
  }

  /* Echo from everyone who has something to say. */
  for (int i = 0; i < MAX_CLIENTS; i++) {
    if (!clients[i]) {
      continue;
    }
    while (clients[i].available()) {
      clients[i].write(clients[i].read());
    }
    if (!clients[i].connected()) {
      Serial.print("client ");
      Serial.print(i);
      Serial.println(" gone");
      clients[i].stop();
      clients[i] = EthernetClient();
    }
  }

  yield();
}
