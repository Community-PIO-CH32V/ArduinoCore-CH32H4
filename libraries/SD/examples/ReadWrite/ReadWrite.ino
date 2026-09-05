/*
   SD card read/write

   This example shows how to read and write data to and from an SD card file.

   created   Nov 2010
   by David A. Mellis
   modified 9 Apr 2012
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

File myFile;

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.print("Initializing SD card...");
  if (!SD.begin()) {
    Serial.println(" failed.");
    return;
  }
  Serial.println(" done.");

  /* open the file. note that only one file can be open at a time,
     so you have to close this one before opening another. */
  myFile = SD.open("test.txt", FILE_WRITE);

  if (myFile) {
    Serial.print("Writing to test.txt...");
    myFile.println("testing 1, 2, 3.");
    myFile.close();
    Serial.println(" done.");
  } else {
    Serial.println("error opening test.txt");
  }

  myFile = SD.open("test.txt");
  if (myFile) {
    Serial.println("test.txt:");
    while (myFile.available()) {
      Serial.write(myFile.read());
    }
    myFile.close();
  } else {
    Serial.println("error opening test.txt");
  }
}

void loop() {
}
