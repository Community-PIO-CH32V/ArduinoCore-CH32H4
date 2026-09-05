/*
   I2S: a sine wave to an external DAC or amplifier.

   This example code is in the public domain.

   ---
   For the CH32H41x core. I2S1 is SPI2: WS on PB12, CK on PB13, SD on PB15.
   I2S(OUTPUT, 1) would be I2S2 instead, on its own pins.

   THE AMPLITUDE BELOW IS DELIBERATELY LOW -- about 5% of full scale. An
   example that comes up at full volume into headphones or a powered speaker
   is unpleasant at best. Raise AMPLITUDE when you know what is on the other
   end and how loud it is.

   write(left, right) takes one frame. I2S is always stereo on the wire even
   when the source is not, so a mono signal is written to both channels; a
   sketch that writes one sample per frame gets half the sample rate and a
   channel that drifts.
*/

#include <I2S.h>

I2S i2s(OUTPUT);

static const int SAMPLE_RATE = 44100;
static const int FREQUENCY = 440;      /* concert A */

/* Out of 32767. Start quiet -- see the note above. */
static const int AMPLITUDE = 1600;

/* A quarter-wave sine table, so nothing needs floating point in the loop. */
static int16_t wave[128];
static size_t wavePos = 0;

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  for (size_t i = 0; i < sizeof(wave) / sizeof(wave[0]); i++) {
    float th = 2.0f * PI * (float)i / (float)(sizeof(wave) / sizeof(wave[0]));
    wave[i] = (int16_t)(AMPLITUDE * sinf(th));
  }

  i2s.setBCLK(PIN_I2S_CK);
  i2s.setDATA(PIN_I2S_SD);
  i2s.setBitsPerSample(16);

  if (!i2s.begin(SAMPLE_RATE)) {
    Serial.println("i2s.begin() failed");
    while (1) {
      delay(1000);
    }
  }
  Serial.println("playing a quiet 440 Hz tone");
}

void loop() {
  /* Step through the table at whatever rate makes FREQUENCY come out. */
  static const size_t TABLE = sizeof(wave) / sizeof(wave[0]);
  static uint32_t phase = 0;
  const uint32_t step = (uint32_t)((uint64_t)FREQUENCY * TABLE * 65536
                                   / SAMPLE_RATE);

  /* Fill whatever room the DMA has, and no more: write() blocks when the
     buffer is full, and availableForWrite() is how a sketch stays responsive
     instead. */
  int room = i2s.availableForWrite();
  while (room > 0) {
    phase += step;
    wavePos = (phase >> 16) % TABLE;
    int16_t s = wave[wavePos];
    i2s.write(s, s);          /* the same sample to both channels */
    room -= 4;                /* two 16-bit samples per frame */
  }
}
