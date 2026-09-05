/*
   Keyboard Message test

   Sends a text string when a button is pressed.

   The circuit:
   - pushbutton attached from pin PE3 to ground

   created 24 Oct 2011
   modified 27 Mar 2012
   by Tom Igoe
   modified 11 Nov 2013
   by Scott Fitzgerald

   This example code is in the public domain.

   https://www.arduino.cc/en/Tutorial/BuiltInExamples/KeyboardMessage

   ---
   For the CH32H41x core, over USB HID. Needs the TinyUSB stack, which is the
   default.

   IT WAITS FOR A BUTTON, and that is not decoration. A board that types (or
   moves the pointer) the moment it enumerates fights you for the machine, and
   reflashing it means catching a window between keystrokes. Gate it on
   something. The button here is PE3 to ground, with the internal pull-up on;
   any spare pin will do.
*/

#include <Keyboard.h>

const int buttonPin = PE3;   /* input pin for the pushbutton */
int previousButtonState = HIGH;
int counter = 0;

void setup() {
  /* The pull-up is internal, so the button only needs a wire to ground. */
  pinMode(buttonPin, INPUT_PULLUP);
  Keyboard.begin();
}

void loop() {
  int buttonState = digitalRead(buttonPin);

  /* Falling edge: pressed, and it was not pressed last time round. */
  if (buttonState == LOW && previousButtonState == HIGH) {
    counter++;
    Keyboard.print("You pressed the button ");
    Keyboard.print(counter);
    Keyboard.println(" times.");
    delay(50);          /* crude debounce */
  }

  previousButtonState = buttonState;
}
