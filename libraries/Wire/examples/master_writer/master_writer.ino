/*
   Wire Master Writer

   Writes data to an I2C/TWI slave device.
   Refer to the "Wire Slave Receiver" example for use with this.

   Created 29 March 2006
   by Nicholas Zambetti <http://www.zambetti.com>

   This example code is in the public domain.

   ---
   For the CH32H41x core. Wire is I2C1 on PB6/PB7. See i2c_scanner for the
   note about pull-ups, which this part does not have.
*/

#include <Wire.h>

void setup() {
  Wire.begin();   /* join I2C bus as a controller */
}

byte x = 0;

void loop() {
  Wire.beginTransmission(8);   /* transmit to device #8 */
  Wire.write("x is ");         /* sends five bytes */
  Wire.write(x);               /* sends one byte */
  Wire.endTransmission();      /* stop transmitting */

  x++;
  delay(500);
}
