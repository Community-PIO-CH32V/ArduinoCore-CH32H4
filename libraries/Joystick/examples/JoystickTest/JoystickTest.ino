/*
   A USB gamepad: axes, buttons and a hat.

   Sweeps the axes, walks the hat round and cycles the buttons, so the whole
   report can be watched in a host-side tester -- Windows' "Set up USB game
   controllers", jstest on Linux, or any browser gamepad page.

   This example code is in the public domain.

   ---
   For the CH32H41x core, over USB HID. Needs the TinyUSB stack, which is the
   default.

   AXES ARE 0..1023 BY DEFAULT, for compatibility with the other Joystick
   libraries -- use8bit(), use10bit() and use16bit() change the range this
   sketch speaks in. The report on the wire is 16-bit signed whichever you
   pick; the mapping happens on this side.

   NOTHING TAKES OVER THE MACHINE HERE. A gamepad is not a pointing device, so
   unlike Keyboard and Mouse this one is safe to leave running.
*/

#include <Joystick.h>

void setup() {
  Joystick.begin();

  /* Batch the updates: several axes and buttons change together below, and
     without this the host sees intermediate states that never really
     existed. send_now() flushes. */
  Joystick.useManualSend(true);
}

void loop() {
  static int angle = 0;
  static uint8_t button = 0;

  /* Two axes going round a circle, in the default 0..1023 range. */
  float a = 2.0f * PI * (float)angle / 360.0f;
  Joystick.X((int)(512 + 500 * cosf(a)));
  Joystick.Y((int)(512 + 500 * sinf(a)));

  /* And two more, out of phase, so all four are distinguishable. */
  Joystick.Z((int)(512 + 500 * cosf(a * 2.0f)));
  Joystick.Zrotate((int)(512 + 500 * sinf(a * 2.0f)));

  /* The hat follows the same angle. -1 would be the rest position. */
  Joystick.hat(angle);

  /* One button at a time, so it is obvious which is which. */
  for (uint8_t i = 0; i < 8; i++) {
    Joystick.setButton(i, i == button);
  }

  Joystick.send_now();

  angle = (angle + 5) % 360;
  if (angle == 0) {
    button = (button + 1) % 8;
  }
  delay(20);
}
