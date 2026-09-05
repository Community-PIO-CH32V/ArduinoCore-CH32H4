/*
   ADCInput: continuous, DMA-fed analog sampling.

   analogRead() is fine for a knob. It is not fine for a signal: each call
   starts a conversion and waits for it, so the sample rate is whatever the
   loop happens to manage and the jitter is whatever else the sketch was doing.

   ADCInput sets the ADC running at a fixed rate with DMA behind it and hands
   the samples over as a Stream. The rate is the hardware's, not the loop's.

   This example code is in the public domain.

   ---
   For the CH32H41x core, in the shape of arduino-pico's ADCInput. This part's
   ADC is 12-bit, so samples run 0..4095.

   ADCInput and analogRead() share one ADC. While this is running, analogRead()
   on a channel it owns returns what the continuous conversion last produced
   rather than starting its own -- which is the right answer, but not the one
   people expect, so it is worth knowing.
*/

#include <ADCInput.h>

/* One channel. ADCInput(A0, A1) would interleave two. */
ADCInput adc(A0);

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  /* 8 kHz. begin() returns false if the rate cannot be reached or the ADC is
     already busy. */
  if (!adc.begin(8000)) {
    Serial.println("adc.begin() failed");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("sampling A0 at 8 kHz");
}

void loop() {
  /* Drain whatever the DMA has delivered since last time, and report the
     range. A peak-to-peak figure is the cheapest way to see that the input is
     actually moving rather than sitting at a rail. */
  int n = 0;
  int lo = 4096, hi = -1;
  long sum = 0;

  while (adc.available()) {
    int s = adc.read();
    if (s < lo) {
      lo = s;
    }
    if (s > hi) {
      hi = s;
    }
    sum += s;
    n++;
  }

  if (n > 0) {
    Serial.print(n);
    Serial.print(" samples  min=");
    Serial.print(lo);
    Serial.print(" max=");
    Serial.print(hi);
    Serial.print(" mean=");
    Serial.println((int)(sum / n));
  } else {
    Serial.println("no samples -- is begin() still running?");
  }

  delay(500);
}
