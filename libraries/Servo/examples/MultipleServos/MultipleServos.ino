/*
   Multiple servos, and what happens when you run out of timers.

   Every Servo needs a timer channel, and this part has a fixed number of them
   spread across a fixed set of pins. attach() returns the channel it managed
   to take, or -1 if the pin has no timer or every channel on it is already
   spoken for -- and a sketch that ignores that gets servos which silently do
   not move.

   This example code is in the public domain.

   ---
   Written for the CH32H41x core. The pins below are TIM1 CH1-CH4, which is
   the easiest group of four to reach on this package; see the variant's
   pins_arduino.h for the rest.
*/

#include <Servo.h>

static const pin_size_t PINS[] = { PA8, PA9, PA10, PA11 };
static const int COUNT = sizeof(PINS) / sizeof(PINS[0]);

Servo servos[COUNT];

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  for (int i = 0; i < COUNT; i++) {
    int ch = servos[i].attach(PINS[i]);
    Serial.print("pin ");
    Serial.print(PINS[i]);
    if (ch < 0) {
      Serial.println(": no timer channel available");
    } else {
      Serial.print(": channel ");
      Serial.println(ch);
      servos[i].write(90);
    }
  }
}

void loop() {
  /* A slow wave across whichever servos actually attached. */
  for (int pos = 0; pos <= 180; pos += 2) {
    for (int i = 0; i < COUNT; i++) {
      if (servos[i].attached()) {
        servos[i].write((pos + i * 45) % 181);
      }
    }
    delay(20);
  }
}
