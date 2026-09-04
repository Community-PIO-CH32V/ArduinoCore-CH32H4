/* The second I2C object. See the note in Wire.h.

   Its default pins are PD12/PD13 on I2C4, chosen so it can share the bus the
   display is already on: I2C is multi-drop, so wiring PB6 to PD12 and PB7 to
   PD13 puts both peripherals and the display on one bus with one set of
   pull-ups. That is what makes an on-board master/slave test possible without
   a second board. */
#include "Wire.h"

TwoWire Wire1(PIN_WIRE_SLAVE_SCL, PIN_WIRE_SLAVE_SDA);
