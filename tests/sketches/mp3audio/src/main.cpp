/* MP3Audio, on Serial1. Nothing here makes a sound.
 *
 * The sink used by every test counts and checksums frames instead of clocking
 * them out of I2S. A real amplifier and speaker are wired to I2S1, and no test
 * needs to hear anything to know whether the decoder works.
 */
#include <Arduino.h>
#include <AudioSink.h>
#include <MP3Decoder.h>
#include <IcyStream.h>
#include <MP3Player.h>
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

/* An AudioSink that counts and checksums instead of making noise.
 *
 * availableFrames() is deliberately huge and underruns() always zero, so the
 * player is never throttled: this measures the player, not the hardware. A
 * real amplifier and speaker are on I2S1 and no test here needs to be heard. */
class CountingSink : public AudioSink {
public:
    bool begin(uint32_t rate) override { _rate = rate; _running = true; return true; }
    bool end() override { _running = false; return true; }
    bool running() const override { return _running; }
    uint32_t sampleRate() const override { return _rate; }
    size_t writeFrame(int16_t l, int16_t r) override {
        if (l != r) { _monoBroken = true; }
        _fnv = fnv1a(_fnv, (const uint8_t *)&l, 2);
        _frames++;
        return 1;
    }
    size_t writeFrames(const int16_t *pcmIn, size_t frames) override {
        for (size_t i = 0; i < frames; i++) {
            if (pcmIn[i * 2] != pcmIn[i * 2 + 1]) { _monoBroken = true; }
            /* Left channel only, so the checksum matches the decoder's over a
               mono source rather than counting every sample twice. */
            _fnv = fnv1a(_fnv, (const uint8_t *)&pcmIn[i * 2], 2);
        }
        _frames += frames;
        return frames;
    }
    size_t availableFrames() override { return 65535; }
    uint32_t underruns() const override { return 0; }

    void reset() { _fnv = 2166136261u; _frames = 0; _monoBroken = false; }
    uint32_t fnv() const { return _fnv; }
    uint32_t frames() const { return _frames; }
    bool monoOk() const { return !_monoBroken; }

private:
    uint32_t _rate = 0, _fnv = 2166136261u, _frames = 0;
    bool _running = false, _monoBroken = false;
};

/* A Stream over an array, so the player can be driven with no network and no
   filesystem. */
class MemStream : public Stream {
public:
    MemStream(const uint8_t *p, size_t n) : _p(p), _n(n) { }
    int available() override { return (int)(_n - _i); }
    int read() override { return _i < _n ? _p[_i++] : -1; }
    int peek() override { return _i < _n ? _p[_i] : -1; }
    void flush() override { }
    size_t write(uint8_t) override { return 0; }
private:
    const uint8_t *_p; size_t _n; size_t _i = 0;
};

static CountingSink sink;
static MP3Player player;

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

  } else if (!strcmp(cmd, "play")) {
    MemStream src(tone_mp3, tone_mp3_len);
    sink.reset();
    player.end();
    player.begin(src, sink);
    uint32_t guard = 0;
    while (player.loop() && ++guard < 200000u) { }
    Serial1.print("rate="); Serial1.println(player.sampleRate());
    Serial1.print("sink_frames="); Serial1.println(sink.frames());
    Serial1.print("pcm_fnv="); Serial1.println(sink.fnv());
    Serial1.print("decode_errors="); Serial1.println(player.decodeErrors());
    Serial1.print("underruns="); Serial1.println(player.underruns());
    Serial1.print("rate_changes="); Serial1.println(player.rateChanges());
    Serial1.print("mono_widened="); Serial1.println(sink.monoOk() ? 1 : 0);
    player.end();

  } else if (!strcmp(cmd, "icytest")) {
    /* 32 bytes of payload, a metadata block, then 32 more. */
    static uint8_t raw[160];
    const char *meta = "StreamTitle='Artist - Track';";
    size_t n = 0;
    for (int i = 0; i < 32; i++) { raw[n++] = (uint8_t)i; }
    const size_t blocks = (strlen(meta) + 15) / 16;
    raw[n++] = (uint8_t)blocks;
    for (size_t i = 0; i < blocks * 16; i++) {
      raw[n++] = i < strlen(meta) ? (uint8_t)meta[i] : 0;
    }
    for (int i = 0; i < 32; i++) { raw[n++] = (uint8_t)(32 + i); }
    MemStream up(raw, n);
    IcyStream icy(up, 32);
    int ok = 1;
    for (int i = 0; i < 64; i++) {
      if (icy.read() != i) { ok = 0; }
    }
    Serial1.print("payload_ok="); Serial1.println(ok);
    Serial1.print("title="); Serial1.println(icy.title());
    Serial1.print("title_changes="); Serial1.println(icy.titleChanges());

  } else if (!strcmp(cmd, "icypass")) {
    static uint8_t raw[64];
    for (int i = 0; i < 64; i++) { raw[i] = (uint8_t)i; }
    MemStream up(raw, 64);
    IcyStream icy(up, 0);
    int ok = 1;
    for (int i = 0; i < 64; i++) { if (icy.read() != i) { ok = 0; } }
    Serial1.print("payload_ok="); Serial1.println(ok);
    Serial1.print("title_changes="); Serial1.println(icy.titleChanges());

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
