/*
   SPI loopback: the wiring test.

   Join MOSI to MISO with a single jumper -- PA7 to PA6 on this part -- and
   every byte written comes straight back. Nothing else is needed, and it
   answers the question that a misbehaving SPI device cannot: is the bus
   working at all?

   With the jumper OUT, MISO floats and reads back as 0x00 or 0xFF depending
   on which way it drifts. That is also worth seeing once, because it is what
   a device that is not responding looks like.

   This example code is in the public domain.

   ---
   Written for the CH32H41x core. PA5, PA6 and PA7 are SPI1 and are on the
   3.3 V rail; the transfer16 and buffer forms are exercised too, because a
   driver that only ever moves single bytes never finds out whether the wider
   paths agree with it.
*/

#include <SPI.h>

static bool checkByte(uint8_t v) {
  uint8_t back = SPI.transfer(v);
  if (back != v) {
    Serial.print("  byte 0x");
    Serial.print(v, HEX);
    Serial.print(" came back 0x");
    Serial.println(back, HEX);
    return false;
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  SPI.begin();
  Serial.println("SPI loopback -- jumper PA7 (MOSI) to PA6 (MISO)");
}

void loop() {
  SPI.beginTransaction(SPISettings(4000000, MSBFIRST, SPI_MODE0));

  bool ok = true;

  /* One byte at a time. */
  for (int i = 0; i < 256; i++) {
    ok &= checkByte((uint8_t)i);
  }

  /* Sixteen bits at a time -- a different register path on most parts, and
     the one where a byte-order mistake shows up. */
  uint16_t w = SPI.transfer16(0xA55A);
  if (w != 0xA55A) {
    Serial.print("  transfer16 came back 0x");
    Serial.println(w, HEX);
    ok = false;
  }

  /* A whole buffer, in place. */
  uint8_t buf[32];
  for (int i = 0; i < 32; i++) {
    buf[i] = (uint8_t)(i * 7);
  }
  SPI.transfer(buf, sizeof(buf));
  for (int i = 0; i < 32; i++) {
    if (buf[i] != (uint8_t)(i * 7)) {
      Serial.print("  buffer byte ");
      Serial.print(i);
      Serial.println(" differs");
      ok = false;
      break;
    }
  }

  SPI.endTransaction();

  Serial.println(ok ? "loopback OK" : "loopback FAILED -- is the jumper on?");
  delay(2000);
}
