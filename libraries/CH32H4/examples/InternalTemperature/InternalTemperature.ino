/*
   The die temperature, and the internal voltage reference.

   Two ADC inputs with no pad behind them. analogReadTemp() converts the first
   into degrees Celsius using the factory calibration; AVREF is the 1.20 V
   band-gap, which is what a sketch reads to work out what its supply voltage
   actually is.

   This example code is in the public domain.

   ---
   For the CH32H41x core.

   THIS IS A DIE TEMPERATURE, not an ambient one. It reads high by however
   much the part is dissipating -- several degrees at 400 MHz with Ethernet
   running. It is good for "is this thing getting hot" and for compensating a
   drift; it is not a thermometer.

   The internal channels need the slow sample window, which analogRead() picks
   for them automatically. On a shorter one they return the previous channel's
   voltage instead -- a reading that looks plausible and is someone else's.
*/

#include <CH32H4.h>

void setup() {
  Serial1.begin(115200);
  delay(200);
  Serial1.println("temp(C)  raw   vref_raw  vdda(V)");
}

void loop() {
  float tempC = analogReadTemp();
  int rawTemp = analogRead(ATEMP);
  int rawVref = analogRead(AVREF);

  /* The band-gap is a known 1.20 V, so whatever fraction of full scale it
     reads tells you what full scale is -- which is the supply. */
  float vdda = (rawVref > 0) ? (1.20f * 4095.0f / (float)rawVref) : 0.0f;

  Serial1.print(tempC, 1);
  Serial1.print("     ");
  Serial1.print(rawTemp);
  Serial1.print("    ");
  Serial1.print(rawVref);
  Serial1.print("      ");
  Serial1.println(vdda, 2);

  delay(1000);
}
