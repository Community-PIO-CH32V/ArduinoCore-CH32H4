/*
   Web client

   This sketch connects to a website and reads the reply.

   created 18 Dec 2009
   by David A. Mellis
   modified 9 Apr 2012
   by Tom Igoe, based on work by Adrian McEwen

   This example code is in the public domain.

   ---
   For the CH32H41x core. EthernetClient here is lwIP, so connect() takes a
   host name and does the DNS lookup itself.

   THE yield() IN THE READ LOOP IS NOT OPTIONAL. lwIP has no thread of its
   own: its timers and its receive path run from yield(), so a loop that waits
   for data without calling it waits forever. delay() calls yield() too, which
   is why so much Arduino networking code appears to work without knowing this.
*/

#include <LwipEthernet.h>

const char server[] = "example.com";

EthernetClient client;

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

  Serial.print("connecting to ");
  Serial.print(server);
  Serial.println("...");

  if (client.connect(server, 80)) {
    Serial.print("connected to ");
    Serial.println(client.remoteIP());
    client.println("GET / HTTP/1.1");
    client.print("Host: ");
    client.println(server);
    client.println("Connection: close");
    client.println();
  } else {
    Serial.println("connection failed");
  }
}

void loop() {
  /* Print whatever the server sends. */
  while (client.available()) {
    Serial.write(client.read());
  }

  /* When the server disconnects and there is nothing left buffered, stop. */
  if (!client.connected() && !client.available()) {
    Serial.println();
    Serial.println("disconnecting.");
    client.stop();
    while (true) {
      delay(1000);
    }
  }

  yield();
}
