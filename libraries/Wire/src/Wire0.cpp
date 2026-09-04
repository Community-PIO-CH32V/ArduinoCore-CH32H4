/* The default I2C object, on its own so that a sketch using only Wire1 does
   not link this one's buffers. See the note in Wire.h. */
#include "Wire.h"

/* PB6 SCL, PB7 SDA -- I2C1, and where the board's SSD1306 lives. */
TwoWire Wire;
