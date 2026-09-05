/*
   SD card datalogger

   This example shows how to log data from three analog sensors
   to an SD card using the SD library.

   created  24 Nov 2010
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

   The analog pins are this part's: A0 to A2. Its ADC is 12-bit, so a reading
   runs to 4095 rather than 1023.
*/

#include <SD.h>

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  Serial.print("Initializing SD card...");
  if (!SD.begin()) {
    Serial.println(" failed -- nothing will be logged.");
    return;
  }
  Serial.println(" done.");
}

void loop() {
  /* make a string for assembling the data to log */
  String dataString = "";

  /* read three sensors and append to the string */
  for (int analogPin = 0; analogPin < 3; analogPin++) {
    int sensor = analogRead(A0 + analogPin);
    dataString += String(sensor);
    if (analogPin < 2) {
      dataString += ",";
    }
  }

  /* open the file. note that only one file can be open at a time,
     so you have to close this one before opening another. */
  File dataFile = SD.open("datalog.txt", FILE_WRITE);

  if (dataFile) {
    dataFile.println(dataString);
    dataFile.close();
    Serial.println(dataString);
  } else {
    Serial.println("error opening datalog.txt");
  }

  delay(1000);
}
