/*
   EEPROM Read

   Reads the value of each byte of the EEPROM and prints it
   to the computer.

   This example code is in the public domain.

   ---
   Adapted for the CH32H41x core: EEPROM.begin(size) comes first, because the
   EEPROM here is emulated in flash and the library has to know how much of it
   to mirror into RAM.
*/

#include <EEPROM.h>

int addr = 0;
byte value;

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  EEPROM.begin(512);
}

void loop() {
  value = EEPROM.read(addr);

  Serial.print(addr);
  Serial.print("\t");
  Serial.print(value, DEC);
  Serial.println();

  addr = addr + 1;
  if (addr == (int)EEPROM.length()) {
    addr = 0;
    Serial.println();
    delay(1000);
  }

  delay(20);
}
