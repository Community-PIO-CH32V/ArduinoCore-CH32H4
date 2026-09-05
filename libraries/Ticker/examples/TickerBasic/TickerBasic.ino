/*
   Ticker: run something on a timer, without blocking.

   A Ticker calls a function every N milliseconds from an interrupt, so loop()
   is free to do something else. It is the answer to "I want this every second
   but delay(1000) stops everything".

   This example code is in the public domain.

   ---
   For the CH32H41x core. The Ticker here is HARDWARE, on TIM4, and that has
   two consequences worth knowing:

     - the callback runs in interrupt context. Keep it short, touch only
       volatile variables, and do not print from it;
     - TIM4 is shared with analogWrite(). The first attach() takes the timer
       and the last detach() gives it back, so a sketch doing both may find
       attach() returning false. It is worth checking.
*/

#include <Ticker.h>

Ticker ticker;

static volatile uint32_t ticks = 0;

void onTick() {
  ticks++;
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  if (!ticker.attach_ms(250, onTick)) {
    Serial.println("attach failed -- is TIM4 in use by analogWrite()?");
  }
}

void loop() {
  Serial.print("ticks = ");
  Serial.println((int)ticks);
  delay(1000);
}
