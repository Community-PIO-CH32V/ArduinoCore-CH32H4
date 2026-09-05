/*
   EEPROM get/put

   Stores and retrieves whole structures and floats rather than single bytes.
   put() writes an object's bytes; get() reads them back into one.

   This example code is in the public domain.

   ---
   Written for the CH32H41x core, in the shape of Arduino's eeprom_get and
   eeprom_put. Kept as one sketch because the pair only makes sense together:
   the point is that what put() wrote is what get() returns, including for a
   struct the library knows nothing about.

   As everywhere in this EEPROM: begin() first, commit() to reach the flash.
*/

#include <EEPROM.h>

struct Settings {
  uint32_t magic;
  float    calibration;
  uint16_t interval_ms;
  char     name[16];
};

static const uint32_t MAGIC = 0x48344545;  /* "H4EE" -- "these are ours" */

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  EEPROM.begin(512);

  Settings s;
  EEPROM.get(0, s);

  if (s.magic != MAGIC) {
    /* First run on this board, or the EEPROM was cleared. A magic number is
       the difference between "no settings yet" and "settings full of whatever
       erased flash reads as", which on this part is 0xE339E339 rather than
       0xFF -- so a check for all-ones would not have caught it. */
    Serial.println("no settings found, writing defaults");
    s.magic = MAGIC;
    s.calibration = 1.0f;
    s.interval_ms = 500;
    strcpy(s.name, "ch32h4");

    EEPROM.put(0, s);
    if (!EEPROM.commit()) {
      Serial.println("commit FAILED");
      return;
    }
  }

  Serial.println("settings:");
  Serial.print("  calibration = ");
  Serial.println(s.calibration, 4);
  Serial.print("  interval_ms = ");
  Serial.println(s.interval_ms);
  Serial.print("  name        = ");
  Serial.println(s.name);

  /* Bump a counter stored after the struct, so the next boot can see that the
     write really survived a power cycle. */
  uint32_t boots = 0;
  EEPROM.get(sizeof(Settings), boots);
  if (boots > 1000000UL) {
    boots = 0;   /* never written before */
  }
  boots++;
  EEPROM.put(sizeof(Settings), boots);
  EEPROM.commit();

  Serial.print("boot count = ");
  Serial.println(boots);
}

void loop() {
}
