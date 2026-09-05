/*
   LittleFS: read and write a file in the flash tail.

   No card, no wiring: LittleFS lives in a partition at the end of this part's
   own flash. It survives a reflash of the sketch, and it is where a sketch
   should keep configuration, logs and small assets.

   This example code is in the public domain.

   ---
   For the CH32H41x core. THE PARTITION HAS TO EXIST, and by default it does
   not -- the sketch gets the whole 912 KB. Give the filesystem some of it:

       PlatformIO:   board_build.filesystem_size = 128k
       Arduino IDE:  Tools > Filesystem size

   With no partition, begin() returns false and says so on Serial rather than
   formatting something that is not there. That is the commonest first failure
   and it is not a bug.
*/

#include <LittleFS.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  if (!LittleFS.begin()) {
    Serial.println("LittleFS.begin() failed -- is the filesystem size set?");
    return;
  }
  Serial.println("LittleFS mounted");

  /* Write */
  File f = LittleFS.open("/hello.txt", "w");
  if (!f) {
    Serial.println("open for write failed");
    return;
  }
  f.println("hello from the flash tail");
  f.printf("built %s %s\n", __DATE__, __TIME__);
  f.close();

  /* Read it back */
  f = LittleFS.open("/hello.txt", "r");
  if (!f) {
    Serial.println("open for read failed");
    return;
  }
  Serial.print("hello.txt is ");
  Serial.print(f.size());
  Serial.println(" bytes:");
  while (f.available()) {
    Serial.write(f.read());
  }
  f.close();

  /* A boot counter, to show that this really survives a power cycle. */
  uint32_t boots = 0;
  f = LittleFS.open("/boots.bin", "r");
  if (f) {
    f.read((uint8_t *)&boots, sizeof(boots));
    f.close();
  }
  boots++;
  f = LittleFS.open("/boots.bin", "w");
  if (f) {
    f.write((const uint8_t *)&boots, sizeof(boots));
    f.close();
  }
  Serial.print("boot count: ");
  Serial.println(boots);
}

void loop() {
}
