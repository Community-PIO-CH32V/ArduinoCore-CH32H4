/*
   I2C Scanner

   Walks every 7-bit address and reports the ones that answer. The first thing
   to run on a new I2C wiring, and the fastest way to tell "the device is not
   responding" from "the device is at a different address than the datasheet
   says".

   This example code is in the public domain.

   ---
   For the CH32H41x core. Wire is I2C1 on PB6 (SCL) and PB7 (SDA).

   THIS PART HAS NO INTERNAL PULL-UPS in open-drain mode, and I2C needs them.
   Without a pair of resistors to 3.3 V -- 4.7 kOhm is the usual answer -- the
   bus never rises, every address reads as an error, and the scan finds
   nothing. Most breakout boards have them fitted already.
*/

#include <Wire.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  Wire.begin();
  Serial.println("I2C scanner");
}

void loop() {
  int found = 0;

  for (uint8_t addr = 1; addr < 127; addr++) {
    Wire.beginTransmission(addr);
    /* 0 means the device ACKed its address. Anything else is silence, a NAK,
       or a bus that never rose. */
    if (Wire.endTransmission() == 0) {
      Serial.print("device at 0x");
      if (addr < 16) {
        Serial.print("0");
      }
      Serial.println(addr, HEX);
      found++;
    }
  }

  if (found == 0) {
    Serial.println("nothing found -- check the pull-ups and the wiring");
  } else {
    Serial.print(found);
    Serial.println(" device(s)");
  }

  delay(5000);
}
