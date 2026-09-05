/*
   Wire Slave Receiver

   Receives data as an I2C/TWI slave device.
   Refer to the "Wire Master Writer" example for use with this.

   Created 29 March 2006
   by Nicholas Zambetti <http://www.zambetti.com>

   This example code is in the public domain.

   ---
   For the CH32H41x core, which has a second I2C exposed as Wire1 -- I2C4 on
   PD12 (SCL) and PD13 (SDA) -- so a board can be its own master and slave with
   two jumpers and no second board. Join PB6-PD12 and PB7-PD13, run
   master_writer on one core... or simply run this and drive it from anything
   else on the bus.

   onReceive() runs in interrupt context. Read the bytes, put them somewhere,
   and leave: Serial.print() from inside it will eventually deadlock against a
   Serial.print() in loop().
*/

#include <Wire.h>

static volatile bool haveLine = false;
static char line[34];
static volatile size_t lineLen = 0;

/* Called when the master has written to us. */
void receiveEvent(int howMany) {
  size_t n = 0;
  while (Wire1.available() && n < sizeof(line) - 1) {
    line[n++] = (char)Wire1.read();
  }
  /* Drain anything that did not fit, or the next frame starts mid-message. */
  while (Wire1.available()) {
    Wire1.read();
  }
  line[n] = '\0';
  lineLen = n;
  haveLine = true;
  (void)howMany;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }
  Wire1.begin(8);                 /* join the bus as slave at address 8 */
  Wire1.onReceive(receiveEvent);
  Serial.println("slave receiver on Wire1, address 8");
}

void loop() {
  if (haveLine) {
    haveLine = false;
    Serial.print("got ");
    Serial.print((int)lineLen);
    Serial.print(" bytes: ");
    Serial.println(line);
  }
}
