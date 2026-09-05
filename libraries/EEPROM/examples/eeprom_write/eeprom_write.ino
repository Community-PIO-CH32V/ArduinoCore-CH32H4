/*
   EEPROM Write

   Stores values read from analog input A0 into the EEPROM.
   These values will stay in the EEPROM when the board is
   turned off and may be retrieved later by another sketch.

   This example code is in the public domain.

   ---
   Adapted for the CH32H41x core. Two differences from the AVR original, both
   because this part has no EEPROM cells and the library emulates them in the
   last 16 KB of flash:

     - EEPROM.begin(size) first. Nothing works before it.
     - EEPROM.commit() to actually write. Until you call it, everything lives
       in a RAM mirror -- which is what makes byte writes cheap, and what stops
       a loop like this one from wearing out a flash page in a minute.
*/

#include <EEPROM.h>

/* The current address in the EEPROM (i.e. which byte we're going to write to
   next) */
int addr = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  EEPROM.begin(512);
  Serial.println("EEPROM write example");
}

void loop() {
  /* Need to divide by 4 because analog inputs range from 0 to 4095 on this
     part and each byte of the EEPROM can only hold a value from 0 to 255. */
  int val = analogRead(A0) / 16;

  EEPROM.write(addr, val);
  Serial.print("addr ");
  Serial.print(addr);
  Serial.print(" = ");
  Serial.println(val);

  addr = addr + 1;
  if (addr == (int)EEPROM.length()) {
    addr = 0;
    /* A page erase and program, once round the whole EEPROM rather than once
       per byte. Nothing above this line touched the flash. */
    if (EEPROM.commit()) {
      Serial.println("committed");
    } else {
      Serial.println("commit FAILED");
    }
  }

  delay(100);
}
