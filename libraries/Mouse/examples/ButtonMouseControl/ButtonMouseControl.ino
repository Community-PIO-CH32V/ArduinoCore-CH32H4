/*
   ButtonMouseControl

   Controls the mouse from five pushbuttons.

   Hardware:
   - five pushbuttons attached to PE3, PE4, PE5, PE6, PB0

   The mouse movement is always relative. This sketch reads four pushbuttons,
   and uses them to set the movement of the mouse.

   WARNING: When you use the Mouse.move() command, the board takes over your
   mouse! Make sure you have control before you use the mouse commands.

   created 15 Mar 2012
   modified 27 Mar 2012
   by Tom Igoe

   This example code is in the public domain.

   https://www.arduino.cc/en/Tutorial/BuiltInExamples/ButtonMouseControl

   ---
   For the CH32H41x core, over USB HID. Needs the TinyUSB stack, which is the
   default.

   THE WARNING ABOVE IS THE IMPORTANT PART. A pointer that will not stay still
   is worse than a keyboard that types: you cannot click the button that
   reflashes the board. Nothing here moves until a button is pressed, which is
   what makes it safe to try.
*/

#include <Mouse.h>

/* set pin numbers for the five buttons */
const int upButton = PE3;
const int downButton = PE4;
const int leftButton = PE5;
const int rightButton = PE6;
const int mouseButton = PB0;

int range = 5;        /* output range of X or Y movement */
int responseDelay = 10;  /* response delay of the mouse, in ms */

void setup() {
  pinMode(upButton, INPUT_PULLUP);
  pinMode(downButton, INPUT_PULLUP);
  pinMode(leftButton, INPUT_PULLUP);
  pinMode(rightButton, INPUT_PULLUP);
  pinMode(mouseButton, INPUT_PULLUP);

  Mouse.begin();
}

void loop() {
  /* Buttons pull to ground, so LOW is pressed. */
  int xDistance = (digitalRead(leftButton) == LOW ? -range : 0)
                  + (digitalRead(rightButton) == LOW ? range : 0);
  int yDistance = (digitalRead(upButton) == LOW ? -range : 0)
                  + (digitalRead(downButton) == LOW ? range : 0);

  if (xDistance != 0 || yDistance != 0) {
    Mouse.move(xDistance, yDistance, 0);
  }

  if (digitalRead(mouseButton) == LOW) {
    if (!Mouse.isPressed(MOUSE_LEFT)) {
      Mouse.press(MOUSE_LEFT);
    }
  } else {
    if (Mouse.isPressed(MOUSE_LEFT)) {
      Mouse.release(MOUSE_LEFT);
    }
  }

  delay(responseDelay);
}
