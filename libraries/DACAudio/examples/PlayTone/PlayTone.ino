/*
   PlayTone - a sine wave out of the internal DACs.

   QUIET ON PURPOSE. AMPLITUDE is a tenth of full scale, because this is the
   first thing anyone runs and the first thing anyone runs should not be at
   full volume into whatever happens to be connected. Raise it once you know
   what is on the other end.

   WIRING. PA4 is left, PA5 is right, and they are raw DAC pins: put an RC
   low-pass on each -- 1 kOhm and 100 nF is a reasonable start, about 1.6 kHz
   -- and feed a HIGH-IMPEDANCE input, such as a powered speaker's line in. Not
   a speaker directly: these pins cannot drive one, and trying is how a pin
   dies.

   Without the filter you hear the 12-bit staircase at the sample rate, which
   sounds like a fault in the driver and is not.

   This example code is in the public domain.
*/

#include <DACAudio.h>
#include <math.h>

DACAudio dac;

const uint32_t RATE = 44100;
const float AMPLITUDE = 0.1f;   /* see above */

/* One period, generated once. 44100 / 100 is 441 Hz, near concert A, and a
   whole number of samples per period -- so the table loops seamlessly. Calling
   sinf() per sample instead would spend most of the sample budget in it. */
const size_t TABLE = 100;
int16_t table[TABLE];

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    delay(10);
  }

  for (size_t i = 0; i < TABLE; i++) {
    const float phase = (2.0f * (float)M_PI * (float)i) / (float)TABLE;
    table[i] = (int16_t)(sinf(phase) * 32767.0f * AMPLITUDE);
  }

  if (!dac.begin(RATE)) {
    Serial.println("DACAudio.begin() failed -- is TIM6 taken, or the rate out of range?");
    return;
  }
  Serial.print("Playing 441 Hz at ");
  Serial.print((int)(AMPLITUDE * 100.0f));
  Serial.println("% into PA4 (left) and PA5 (right).");
}

void loop() {
  if (!dac.running()) {
    return;
  }
  /* Top the ring up with whatever room there is. The sink never blocks, so
     this returns promptly however busy the output is, and loop() stays free to
     do other work. */
  static size_t phase = 0;
  while (dac.availableFrames() > 0) {
    const int16_t s = table[phase];
    if (!dac.writeFrame(s, s)) {
      break;
    }
    phase = (phase + 1 == TABLE) ? 0 : phase + 1;
  }

  static uint32_t last = 0;
  if (millis() - last > 5000) {
    last = millis();
    Serial.print("underruns: ");
    Serial.println(dac.underruns());
  }
}
