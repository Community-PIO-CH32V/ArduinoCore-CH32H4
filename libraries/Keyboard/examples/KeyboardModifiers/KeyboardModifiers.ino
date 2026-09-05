/*
   Modifiers, key constants, and the media keys.

   press() holds a key down; releaseAll() lets everything go. That is how a
   combination is sent -- Ctrl held while another key is tapped -- and it is
   also how a sketch leaves a key stuck down if it forgets the release.

   This example code is in the public domain.

   ---
   For the CH32H41x core, over USB HID.

   IT WAITS FOR A BUTTON, and that is not decoration. A board that types (or
   moves the pointer) the moment it enumerates fights you for the machine, and
   reflashing it means catching a window between keystrokes. Gate it on
   something. The button here is PE3 to ground, with the internal pull-up on;
   any spare pin will do.

   The consumer keys -- volume, play/pause -- are a different HID page and go
   through consumerPress()/consumerRelease() rather than press(). They are on
   the same interface, distinguished by a report ID.
*/

#include <Keyboard.h>

const int buttonPin = PE3;
int previousButtonState = HIGH;
int step = 0;

void setup() {
  pinMode(buttonPin, INPUT_PULLUP);
  Keyboard.begin();
}

static void selectAll() {
  Keyboard.press(KEY_LEFT_CTRL);
  Keyboard.press('a');
  delay(20);
  Keyboard.releaseAll();      /* both, always -- a held key is sticky */
}

static void altTab() {
  Keyboard.press(KEY_LEFT_ALT);
  Keyboard.press(KEY_TAB);
  delay(20);
  Keyboard.releaseAll();
}

static void volumeUp() {
  /* A different HID usage page: not a keyboard key at all. */
  Keyboard.consumerPress(HID_USAGE_CONSUMER_VOLUME_INCREMENT);
  delay(20);
  Keyboard.consumerRelease();
}

void loop() {
  int buttonState = digitalRead(buttonPin);

  if (buttonState == LOW && previousButtonState == HIGH) {
    switch (step % 3) {
      case 0: selectAll(); break;
      case 1: altTab();    break;
      case 2: volumeUp();  break;
    }
    step++;
    delay(50);
  }

  previousButtonState = buttonState;
}
