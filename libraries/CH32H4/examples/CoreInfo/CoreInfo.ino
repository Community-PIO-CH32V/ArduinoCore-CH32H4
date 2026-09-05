/*
   What this chip is, asked at run time.

   Core number, clocks, heap, unique ID, die temperature -- the things a
   sketch prints once at boot and then wants in a bug report.

   This example code is in the public domain.

   ---
   For the CH32H41x core. Note the two clocks, which are not the same number
   and are the commonest thing to confuse:

     getCpuFreqHz()  what THIS core runs at -- 400 MHz on the V5F, 100 MHz on
                     the V3F.
     getBusFreqHz()  the bus, 100 MHz for both. This is the one a peripheral
                     divider works from. A baud rate or a timer prescaler
                     computed from the CPU clock is four times off on the V5F,
                     and every register downstream agrees with itself, so
                     nothing catches it but the wire.
*/

#include <CH32H4.h>

void report(const char *who) {
  Serial1.print("[");
  Serial1.print(who);
  Serial1.print("] core=");
  Serial1.print(CH32H4.getCoreNum());
  Serial1.print(" cpu=");
  Serial1.print(CH32H4.getCpuFreqHz() / 1000000);
  Serial1.print(" MHz  bus=");
  Serial1.print(CH32H4.getBusFreqHz() / 1000000);
  Serial1.print(" MHz  heap=");
  Serial1.print((unsigned)CH32H4.getFreeHeap());
  Serial1.println(" bytes");
}

void setup() {
  Serial1.begin(115200);
  delay(200);

  uint8_t id[8];
  CH32H4.getUniqueId(id);
  Serial1.print("unique id: ");
  for (int i = 0; i < 8; i++) {
    if (id[i] < 16) {
      Serial1.print("0");
    }
    Serial1.print(id[i], HEX);
  }
  Serial1.println();

  report("V5F");
}

void loop() {
  Serial1.print("die temperature: ");
  Serial1.print(analogReadTemp(), 1);
  Serial1.println(" C");
  delay(2000);
}

/* The second core. Defining setup1() is all it takes to start it. */
void setup1() {
  delay(400);          /* let the V5F print its banner first */
  report("V3F");
}

void loop1() {
  delay(5000);
}
