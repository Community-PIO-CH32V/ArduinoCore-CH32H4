/*
   SDFS: the SD card as a filesystem, rather than as the classic SD library.

   SD.h is the old Arduino API kept for compatibility -- 8.3 names, one open
   file at a time in spirit if not in law. SDFS is the same FS interface that
   LittleFS presents, which means a sketch can be written once and pointed at
   either.

   This example code is in the public domain.

   ---
   For the CH32H41x core. There is no chip-select pin and no SPI bus: this
   part has a real SDMMC controller, so the configuration is bus width and
   clock.

       SDFS.setConfig(SDFSConfig(1, 20000000));   // 1-bit, 20 MHz
       SDFS.setConfig(SDFSConfig(4, 25000000));   // 4-bit, if DAT1-3 are wired

   setConfig() must come before begin(); afterwards it does nothing, because
   the card is already initialised at the old settings.
*/

#include <SDFS.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  SDFS.setConfig(SDFSConfig(1, 20000000));

  if (!SDFS.begin()) {
    Serial.println("SDFS.begin() failed -- is a card inserted and formatted?");
    return;
  }
  Serial.println("card mounted");

  FSInfo info;
  if (SDFS.info(info)) {
    Serial.print("total ");
    Serial.print((unsigned long)(info.totalBytes / 1024 / 1024));
    Serial.print(" MB, used ");
    Serial.print((unsigned long)(info.usedBytes / 1024 / 1024));
    Serial.println(" MB");
  }

  /* Write */
  File f = SDFS.open("/sdfs-demo.txt", "a");
  if (!f) {
    Serial.println("open for append failed");
    return;
  }
  f.printf("booted at %lu ms\n", (unsigned long)millis());
  f.close();

  /* Read the whole thing back */
  f = SDFS.open("/sdfs-demo.txt", "r");
  Serial.print("/sdfs-demo.txt (");
  Serial.print(f.size());
  Serial.println(" bytes):");
  while (f.available()) {
    Serial.write(f.read());
  }
  f.close();

  /* And walk the root */
  Serial.println("\nroot directory:");
  Dir dir = SDFS.openDir("/");
  while (dir.next()) {
    Serial.print("  ");
    Serial.print(dir.fileName());
    if (dir.isDirectory()) {
      Serial.println("/");
    } else {
      Serial.print("  ");
      Serial.print(dir.fileSize());
      Serial.println(" bytes");
    }
  }
}

void loop() {
}
