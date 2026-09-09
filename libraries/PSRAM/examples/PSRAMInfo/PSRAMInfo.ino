/*
   PSRAMInfo - identify the external PSRAM and measure it.

   WIRING. An ESP-PSRAM64H (AP Memory APS6404L) on QSPI2:

       SCLK PE10   CE#  PE11   IO0 PE12
       IO1  PE13   IO2  PE14   IO3 PE15

   plus 3.3 V, ground, a 10 kOhm pull-up on CE#, and 100 nF of decoupling
   close to the chip. Those six pins reach QSPI2 on alternate function 7.

   READING THE FAILURE. begin() returning false has two quite different
   causes, and detected() tells them apart:

       begun=0 detected=0   the chip did not answer -- check the wiring
       begun=0 detected=1   the chip is there but the clock was unusable

   Identification is exact rather than approximate. The manufacturer ID 0x0D
   and the known-good-die marker 0x5D are fixed in the silicon, so a floating
   bus cannot fake them. A "did anything come back" check would pass on a
   disconnected chip; this does not.

   This example code is in the public domain.
*/

#include <PSRAM.h>

/* 4 KB of RAM to move 64 KB with. Nothing here needs a buffer the size of the
   transfer -- that is rather the point of an 8 MB device. */
static uint8_t chunk[4096] __attribute__((aligned(4)));

static uint8_t pattern(uint32_t addr) {
  return (uint8_t)(addr * 31u + (addr >> 13));
}

/* Write a pattern at one address and read it straight back. */
static bool verifyAt(uint32_t addr) {
  static uint8_t out[64], in[64];
  for (uint32_t i = 0; i < sizeof(out); i++) { out[i] = pattern(addr + i); }
  if (PSRAM.write(addr, out, sizeof(out)) != sizeof(out)) { return false; }
  memset(in, 0, sizeof(in));
  if (PSRAM.read(addr, in, sizeof(in)) != sizeof(in)) { return false; }
  return memcmp(out, in, sizeof(out)) == 0;
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { }

  Serial.println();
  Serial.println("PSRAM");
  Serial.println("-----");

  if (!PSRAM.begin()) {
    Serial.print("begin() failed, detected=");
    Serial.println(PSRAM.detected() ? 1 : 0);
    Serial.println(PSRAM.detected() ? "  the chip answered, so the clock was the problem"
                                    : "  no answer on PE10..PE15 -- check the wiring");
    return;
  }

  Serial.print("size        "); Serial.print((uint32_t)(PSRAM.size() >> 20));
  Serial.println(" MB");
  Serial.print("clock       "); Serial.print(PSRAM.clock() / 1000000);
  Serial.println(" MHz");
  Serial.print("maker       0x"); Serial.println(PSRAM.manufacturerID(), HEX);

  uint8_t id[6];
  PSRAM.eid(id);
  Serial.print("die serial  ");
  for (int i = 0; i < 6; i++) {
    if (id[i] < 16) { Serial.print('0'); }
    Serial.print(id[i], HEX);
  }
  Serial.println();

  /* Both ends of the array. The bottom alone would pass on a device that is
     smaller than it claims, because the top would simply alias onto it. */
  Serial.print("bottom      ");
  Serial.println(verifyAt(0) ? "ok" : "FAILED");
  Serial.print("top         ");
  Serial.println(verifyAt(PSRAMClass::CAPACITY - 64) ? "ok" : "FAILED");

  /* Fill 64 KB, then time reading it back. Every transfer is DMA, so this is
     the device's real throughput and not a measurement of the copy loop. */
  for (uint32_t off = 0; off < 65536; off += sizeof(chunk)) {
    for (uint32_t i = 0; i < sizeof(chunk); i++) { chunk[i] = pattern(off + i); }
    PSRAM.write(off, chunk, sizeof(chunk));
  }

  uint32_t bad = 0;
  const uint32_t t0 = micros();
  for (uint32_t off = 0; off < 65536; off += sizeof(chunk)) {
    PSRAM.read(off, chunk, sizeof(chunk));
    for (uint32_t i = 0; i < sizeof(chunk); i++) {
      if (chunk[i] != pattern(off + i)) { bad++; }
    }
  }
  const uint32_t us = micros() - t0;

  /* The verify is inside the timed loop, so this number is pessimistic --
     honest about what a sketch actually gets rather than a best case with the
     checking removed. */
  Serial.print("read 64 KB  "); Serial.print(us); Serial.print(" us, ");
  Serial.print(65536u / us); Serial.println(" MB/s (verify included)");
  Serial.print("errors      "); Serial.println(bad);
}

void loop() {
}
