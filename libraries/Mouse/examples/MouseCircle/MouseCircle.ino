/*
   Draw a circle with the pointer, once per button press.

   A bounded gesture rather than a loop that keeps moving: the pointer returns
   roughly where it started and then stops, so the machine stays usable.

   This example code is in the public domain.

   ---
   For the CH32H41x core, over USB HID.

   WHY THE MOTION IS SPLIT INTO SMALL STEPS. A HID mouse report carries one
   signed byte per axis, so -127..127 is the whole range of a single report.
   Mouse.move() splits a larger ask across several, but a circle wants small
   steps anyway or it comes out as a polygon.

   Nothing moves until the button on PE3 is pressed.
*/

#include <Mouse.h>

const int buttonPin = PE3;
int previousButtonState = HIGH;

void setup() {
  pinMode(buttonPin, INPUT_PULLUP);
  Mouse.begin();
}

static void circle(int radius, int steps) {
  int prevX = radius;
  int prevY = 0;
  for (int i = 1; i <= steps; i++) {
    float a = 2.0f * PI * (float)i / (float)steps;
    int x = (int)(radius * cosf(a));
    int y = (int)(radius * sinf(a));
    Mouse.move(x - prevX, y - prevY, 0);
    prevX = x;
    prevY = y;
    delay(10);
  }
}

void loop() {
  int buttonState = digitalRead(buttonPin);

  if (buttonState == LOW && previousButtonState == HIGH) {
    circle(60, 48);
    delay(50);
  }

  previousButtonState = buttonState;
}
