/*
   Wire Slave Sender

   Sends data as an I2C/TWI slave device.
   Refer to the "Wire Master Reader" example for use with this.

   Created 29 March 2006
   by Nicholas Zambetti <http://www.zambetti.com>

   This example code is in the public domain.

   ---
   For the CH32H41x core, on Wire1 (I2C4, PD12/PD13).

   onRequest() runs in interrupt context and must answer immediately: whatever
   it write()s is what the master clocks out of this transaction. There is no
   opportunity to go and fetch something -- the master is already clocking. So
   keep the answer somewhere loop() updates, and have the callback do nothing
   but hand it over.
*/

#include <Wire.h>

static volatile uint32_t counter = 0;

/* Called when the master asks us for data. */
void requestEvent() {
  char buf[8];
  buf[0] = (uint8_t)(counter >> 24);
  buf[1] = (uint8_t)(counter >> 16);
  buf[2] = (uint8_t)(counter >> 8);
  buf[3] = (uint8_t)(counter);
  Wire1.write((const uint8_t *)buf, 4);
}

void setup() {
  Wire1.begin(8);                 /* join the bus as slave at address 8 */
  Wire1.onRequest(requestEvent);
}

void loop() {
  counter++;
  delay(100);
}
