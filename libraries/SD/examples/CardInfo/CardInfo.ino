/*
   SD card test

   This example shows how use the utility libraries on which the SD library
   is based in order to get info about your SD card. Very useful for testing
   a card when you're not sure whether its working or not.

   created  28 Mar 2011
   by Limor Fried
   modified 24 July 2020
   by Tom Igoe

   This example code is in the public domain.

   ---
   For the CH32H41x core, and begin() is where it differs from the stock
   library. The stock SD is SPI and takes a chip-select pin; this part has a
   real SDMMC controller, so there is no CS and no SPI bus. begin() takes the
   bus width and the clock instead:

       SD.begin();               // 1-bit at 20 MHz, the safe default
       SD.begin(4, 25000000);    // 4-bit at 25 MHz, if all four DAT lines
                                 // are wired

   SD.begin(csPin) is still accepted and ignores the pin, so sketches written
   for the stock library compile unchanged -- but it is not doing what the
   name says, which is why the forms above exist.
*/

#include <SD.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.print("Initializing SD card...");

  if (!SD.begin()) {
    Serial.println(" failed.");
    Serial.println("Things to check:");
    Serial.println("* is a card inserted?");
    Serial.println("* is the wiring correct?");
    Serial.println("* is the card formatted FAT16/FAT32?");
    return;
  }
  Serial.println(" done.");

  Serial.print("Card type: ");
  switch (SD.type()) {
    case 1:  Serial.println("SD1"); break;
    case 2:  Serial.println("SD2"); break;
    case 3:  Serial.println("SDHC/SDXC"); break;
    default: Serial.println("unknown");
  }

  Serial.print("Volume size (bytes): ");
  Serial.println((unsigned long)SD.size64());

  Serial.println("\nFiles found on the card:");
  File root = SD.open("/");
  printDirectory(root, 0);
  root.close();
}

void printDirectory(File dir, int numTabs) {
  while (true) {
    File entry = dir.openNextFile();
    if (!entry) {
      break;
    }
    for (uint8_t i = 0; i < numTabs; i++) {
      Serial.print('\t');
    }
    Serial.print(entry.name());
    if (entry.isDirectory()) {
      Serial.println("/");
      printDirectory(entry, numTabs + 1);
    } else {
      Serial.print("\t\t");
      Serial.println(entry.size(), DEC);
    }
    entry.close();
  }
}

void loop() {
}
