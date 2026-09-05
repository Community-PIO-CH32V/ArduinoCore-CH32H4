/* Sweep
   by BARRAGAN <http://barraganstudio.com>
   This example code is in the public domain.

   modified 8 Nov 2013
   by Scott Fitzgerald
   https://www.arduino.cc/en/Tutorial/LibraryExamples/Sweep

   ---
   Unmodified for the CH32H41x core except for the pin, which has no
   convention here: any pin that has a timer channel will do, and PA8 is TIM1
   CH1. Servo::attach() returns the channel it took, or -1 if that pin has no
   timer -- worth checking, because a servo that never moves and a servo on a
   pin with no timer look identical.
*/

#include <Servo.h>

Servo myservo;   /* create servo object to control a servo */

int pos = 0;     /* variable to store the servo position */

void setup() {
  myservo.attach(PA8);   /* attaches the servo on PA8 to the servo object */
}

void loop() {
  for (pos = 0; pos <= 180; pos += 1) {  /* goes from 0 degrees to 180 degrees */
    myservo.write(pos);                  /* tell servo to go to position 'pos' */
    delay(15);                           /* waits 15ms to reach the position */
  }
  for (pos = 180; pos >= 0; pos -= 1) {  /* goes from 180 degrees to 0 degrees */
    myservo.write(pos);
    delay(15);
  }
}
