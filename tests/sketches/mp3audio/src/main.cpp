/* MP3Audio, on Serial1. Nothing here makes a sound.
 *
 * The sink used by every test counts and checksums frames instead of clocking
 * them out of I2S. A real amplifier and speaker are wired to I2S1, and no test
 * needs to hear anything to know whether the decoder works.
 */
#include <Arduino.h>
#include <MP3Decoder.h>
#include "tone_mp3.h"

static char line[96];
static int len = 0;

static uint32_t fnv1a(uint32_t h, const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    h = (h ^ p[i]) * 16777619u;
  }
  return h;
}

static MP3Decoder decoder;
static int16_t pcm[MP3Decoder::MAX_SAMPLES];

static void handle(const char *cmd) {
  if (!strcmp(cmd, "mp3info")) {
    Serial1.print("mp3_len="); Serial1.println(tone_mp3_len);
    Serial1.print("mp3_fnv=");
    Serial1.println(fnv1a(2166136261u, tone_mp3, tone_mp3_len));

  } else if (!strcmp(cmd, "decode")) {
    decoder.end();
    if (!decoder.begin()) {
      Serial1.println("began=0"); Serial1.print("> "); return;
    }
    uint32_t h = 2166136261u;
    size_t off = 0;
    const uint32_t t0 = micros();
    while (off < tone_mp3_len) {
      size_t used = 0;
      const int n = decoder.decodeFrame(tone_mp3 + off, tone_mp3_len - off,
                                        &used, pcm, MP3Decoder::MAX_SAMPLES);
      if (used == 0 && n <= 0) { break; }   /* out of data */
      off += used;
      if (n > 0) { h = fnv1a(h, (const uint8_t *)pcm, (size_t)n * 2); }
    }
    const uint32_t dus = micros() - t0;
    Serial1.print("decode_us="); Serial1.println(dus);
    Serial1.print("rate="); Serial1.println(decoder.sampleRate());
    Serial1.print("channels="); Serial1.println(decoder.channels());
    Serial1.print("bitrate="); Serial1.println(decoder.bitrate());
    Serial1.print("frames="); Serial1.println(decoder.frames());
    Serial1.print("errors="); Serial1.println(decoder.errors());
    Serial1.print("pcm_fnv="); Serial1.println(h);

  } else if (!strcmp(cmd, "actlr")) {
    const uint32_t a = FLASH->ACTLR;
    Serial1.print("actlr=0x"); Serial1.println(a, HEX);
    Serial1.print("sck_cfg="); Serial1.println(a & 3u);
    Serial1.print("enhance_status="); Serial1.println((a >> 6) & 1u);
    Serial1.print("ehmod="); Serial1.println((a >> 7) & 1u);
    Serial1.print("rd_md="); Serial1.println((a >> 11) & 1u);

  } else {
    Serial1.print("unknown: "); Serial1.println(cmd);
  }
  Serial1.print("> ");
}

void setup() {
  Serial1.begin(115200);
  Serial1.println();
  Serial1.println("mp3audio test");
  Serial1.print("> ");
}

void loop() {
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (len) { line[len] = '\0'; handle(line); len = 0; }
    } else if (len < (int)sizeof(line) - 1) {
      line[len++] = c;
    }
  }
  yield();
}
