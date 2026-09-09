/*
   BigBuffer - a 1 MB array on a part with 896 KB of RAM.

   This is the example that makes the case for the chip. It builds half a
   million 16-bit samples -- 1 MB, more than all of the internal SRAM, and far
   more than a sketch could ever malloc -- then reads every one of them back
   and sums them. At no point does more than 8 KB of RAM hold audio.

   WORKING WITH A DEVICE YOU CANNOT POINT AT. There is deliberately no
   pointer into the PSRAM: read() and write() move blocks, and the sketch
   works on a window at a time. That is the shape every access to this device
   should take. It is not a limitation to route around -- moving a block is
   what runs at full speed, and a chunk this size amortises the per-transfer
   cost to nothing.

   Sizing the window is the only real decision. Bigger windows are not
   meaningfully faster past a few kilobytes, because the transfer is already
   at the line rate and only the fixed cost per call is being spread; they
   just consume RAM you wanted the device for in the first place.

   WIRING is in the PSRAMInfo example.

   This example code is in the public domain.
*/

#include <PSRAM.h>

/* 512 Ki samples of 2 bytes = 1 MB. */
const uint32_t SAMPLES = 512u * 1024u;
const uint32_t BYTES = SAMPLES * sizeof(int16_t);

/* The window. 4096 samples is 8 KB -- under 1% of the array. */
const uint32_t WINDOW = 4096;
static int16_t window[WINDOW];

/* A cheap triangle, so the expected sum is known exactly and the check is a
   real one rather than "it returned something". */
static int16_t sampleAt(uint32_t i) {
  const uint32_t phase = i & 1023u;
  return (int16_t)(phase < 512 ? (int)phase - 256 : 767 - (int)phase);
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { }

  Serial.println();
  if (!PSRAM.begin()) {
    Serial.println("no PSRAM -- see the PSRAMInfo example");
    return;
  }

  Serial.print("array   "); Serial.print(BYTES >> 10);
  Serial.print(" KB in a device of "); Serial.print((uint32_t)(PSRAM.size() >> 20));
  Serial.println(" MB");
  Serial.print("window  "); Serial.print(sizeof(window));
  Serial.println(" bytes of RAM");

  uint32_t t0 = micros();
  for (uint32_t i = 0; i < SAMPLES; i += WINDOW) {
    for (uint32_t j = 0; j < WINDOW; j++) { window[j] = sampleAt(i + j); }
    PSRAM.write(i * sizeof(int16_t), window, sizeof(window));
  }
  const uint32_t fillUs = micros() - t0;

  /* Sum in 64-bit, because 512 Ki samples of up to 32767 would overflow a
     32-bit accumulator on a waveform that did not happen to cancel. */
  int64_t sum = 0;
  uint32_t bad = 0;
  t0 = micros();
  for (uint32_t i = 0; i < SAMPLES; i += WINDOW) {
    PSRAM.read(i * sizeof(int16_t), window, sizeof(window));
    for (uint32_t j = 0; j < WINDOW; j++) {
      if (window[j] != sampleAt(i + j)) { bad++; }
      sum += window[j];
    }
  }
  const uint32_t readUs = micros() - t0;

  Serial.print("filled  "); Serial.print(fillUs / 1000); Serial.print(" ms, ");
  Serial.print(BYTES / fillUs); Serial.println(" MB/s");
  Serial.print("read    "); Serial.print(readUs / 1000); Serial.print(" ms, ");
  Serial.print(BYTES / readUs); Serial.println(" MB/s (verify included)");
  Serial.print("sum     "); Serial.println((long)sum);
  Serial.print("errors  "); Serial.println(bad);

  /* The triangle is symmetric over each 1024-sample period, so a whole number
     of periods sums to a known constant. Getting this right is what says the
     data survived the round trip, rather than merely that a read returned. */
  Serial.print("expected ");
  Serial.println(bad == 0 ? "0 errors, sum -262144" : "0 errors");
}

void loop() {
}
