/*
   Blink: the built-in LED on for half a second, then off for half a second.

   On the CH32H417QEU6-R0-1v1 evaluation board LED_BUILTIN is LED1, on PE2.

   This example code is in the public domain.
*/

void setup() {
  pinMode(LED_BUILTIN, OUTPUT);
}

void loop() {
  digitalWrite(LED_BUILTIN, HIGH);
  delay(500);
  digitalWrite(LED_BUILTIN, LOW);
  delay(500);
}
