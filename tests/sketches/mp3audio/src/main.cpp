/* MP3Audio, on Serial1. Nothing here makes a sound.
 *
 * The sink used by every test counts and checksums frames instead of clocking
 * them out of I2S. A real amplifier and speaker are wired to I2S1, and no test
 * needs to hear anything to know whether the decoder works.
 */
#include <Arduino.h>
#include <LwipEthernet.h>
#include <EthernetClientSecure.h>
#include <HTTPClient.h>

extern "C" {
#include "ch32h4_rtc.h"
}

/* Fed in a line at a time: a PEM does not fit one command line, and baking one
   into the firmware would make the test depend on a certificate that
   expires. */
static char ca_pem[2048];
static size_t ca_len = 0;
#include <AudioSink.h>
#include <MP3Decoder.h>
#include <IcyStream.h>
#include <PlaylistURL.h>
#include <RadioStream.h>
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
            const int32_t a = pcmIn[i * 2] < 0 ? -pcmIn[i * 2] : pcmIn[i * 2];
            if ((uint32_t)a > _peak) { _peak = (uint32_t)a; }
        }
        _frames += frames;
        return frames;
    }
    size_t availableFrames() override { return 65535; }
    uint32_t peak() const { return _peak; }
    uint32_t underruns() const override { return 0; }

    void reset() { _fnv = 2166136261u; _frames = 0; _peak = 0; _monoBroken = false; }
    uint32_t fnv() const { return _fnv; }
    uint32_t frames() const { return _frames; }
    bool monoOk() const { return !_monoBroken; }

private:
    uint32_t _rate = 0, _fnv = 2166136261u, _frames = 0, _peak = 0;
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

  } else if (!strncmp(cmd, "playvol ", 8)) {
    /* playvol <0..1> -- play at a volume and report the peak amplitude. */
    MemStream src(tone_mp3, tone_mp3_len);
    sink.reset();
    player.end();
    player.setVolume((float)atof(cmd + 8));
    player.begin(src, sink);
    uint32_t guard = 0;
    while (player.loop() && ++guard < 200000u) { }
    Serial1.print("volume="); Serial1.println((uint32_t)(player.volume() * 1000.0f));
    Serial1.print("peak="); Serial1.println(sink.peak());
    Serial1.print("sink_frames="); Serial1.println(sink.frames());
    player.end();
    player.setVolume(1.0f);

  } else if (!strncmp(cmd, "playpct ", 8)) {
    /* playpct <0..100> -- the integer percent knob, which is what the
       WebRadio example's two-digit console command drives. */
    MemStream src(tone_mp3, tone_mp3_len);
    sink.reset();
    player.end();
    player.setVolumePercent((uint8_t)strtoul(cmd + 8, nullptr, 10));
    player.begin(src, sink);
    uint32_t guard = 0;
    while (player.loop() && ++guard < 200000u) { }
    Serial1.print("pct="); Serial1.println(player.volumePercent());
    Serial1.print("peak="); Serial1.println(sink.peak());
    player.end();
    player.setVolume(1.0f);

  } else if (!strncmp(cmd, "radio ", 6)) {
    /* radio <url> -- the whole resolve-and-play path, playlists included.
       This is the same RadioStream the WebRadio example uses, which is why it
       lives in the library rather than in the example. */
    static RadioStream radio;
    radio.setInsecure();
    sink.reset();
    player.end();
    radio.end();
    if (!radio.begin(cmd + 6, &Serial1)) {
      Serial1.println("radio_ok=0");
      Serial1.print("status="); Serial1.println(radio.status());
      Serial1.print("> "); return;
    }
    Serial1.println("radio_ok=1");
    Serial1.print("hops="); Serial1.println(radio.hops());
    Serial1.print("final_url="); Serial1.println(radio.url());
    player.begin(radio.stream(), sink);
    const uint32_t rstart = millis();
    while (player.loop() && millis() - rstart < 60000u) { }
    Serial1.print("pcm_fnv="); Serial1.println(sink.fnv());
    Serial1.print("sink_frames="); Serial1.println(sink.frames());
    Serial1.print("decode_errors="); Serial1.println(player.decodeErrors());
    /* Before radio.end(): the IcyStream these come from lives inside it. */
    Serial1.print("metaint="); Serial1.println(radio.metaint());
    Serial1.print("title_changes="); Serial1.println(radio.titleChanges());
    Serial1.print("title="); Serial1.println(radio.title());
    player.end();
    radio.end();

  } else if (!strncmp(cmd, "pl ", 3)) {
    /* pl <name> -- resolve one synthetic playlist body. No network. */
    const char *name = cmd + 3;
    const char *body = nullptr;
    const char *exclude = nullptr;
    if (!strcmp(name, "m3u")) {
      body = "#EXTM3U\n#EXTINF:-1,Antenne\nhttp://mp3.antenne.de/antenne\n";
    } else if (!strcmp(name, "pls")) {
      body = "[playlist]\nnumberofentries=1\n"
             "File1=http://streamer.radio.co/s6117a960f/listen\n"
             "Title1=Some Station\nLength1=-1\n";
    } else if (!strcmp(name, "asx")) {
      /* One long line, which is how stations actually send these. */
      body = "<asx version=\"3.0\"><entry>"
             "<ref href=\"http://radio.kahoku.net:8000\"/></entry></asx>";
    } else if (!strcmp(name, "ref")) {
      /* A [Reference] file's first entry points back at itself, so the
         exclude argument is what makes the second one win. */
      body = "[Reference]\nRef1=http://self.example/list.asx\n"
             "Ref2=http://stream.laut.fm/kawaii-music\n";
      exclude = "http://self.example/list.asx";
    } else if (!strcmp(name, "audio")) {
      /* Not a playlist: MP3 frames. Must be rejected, not mis-parsed. */
      body = "\xff\xfb\x50\xc4 rubbish that is not a url at all";
    } else {
      Serial1.println("found=0"); Serial1.print("> "); return;
    }
    char url[256];
    const bool ok = PlaylistURL::first(body, strlen(body), exclude,
                                       url, sizeof(url));
    Serial1.print("found="); Serial1.println(ok ? 1 : 0);
    Serial1.print("url="); Serial1.println(ok ? url : "");

  } else if (!strncmp(cmd, "ctype ", 6)) {
    /* ctype <content-type>[|icy] -- is this audio or a pointer to it? */
    char buf[96];
    strncpy(buf, cmd + 6, sizeof(buf) - 1);
    buf[sizeof(buf) - 1] = 0;
    char *bar = strchr(buf, '|');
    bool icy = false;
    if (bar) { *bar = 0; icy = true; }
    Serial1.print("audio=");
    Serial1.println(PlaylistURL::isAudio(buf, icy) ? 1 : 0);

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

  } else if (!strncmp(cmd, "netplay ", 8)) {
    /* Fetch an MP3 over HTTP and play it into the counting sink.
       The URL points at a server on the test machine, not at this board --
       lwIP here is built without LWIP_NETIF_LOOPBACK, so a packet addressed to
       our own IP leaves the Ethernet port and never comes back. */
    static EthernetClient client;
    static HTTPClient http;
    if (!http.begin(client, String(cmd + 8))) {
      Serial1.println("http_rc=-1000"); Serial1.print("> "); return;
    }
    /* BEFORE GET(). HTTPClient keeps only the headers it was told to keep, and
       a metaint that silently reads as zero turns metadata into corrupt
       audio. */
    static const char *keys[] = { "icy-metaint" };
    http.collectHeaders(keys, 1);
    const int rc = http.GET();
    Serial1.print("http_rc="); Serial1.println(rc);
    if (rc != 200) { http.end(); Serial1.print("> "); return; }

    const uint32_t metaint = (uint32_t)http.header("icy-metaint").toInt();
    IcyStream icy(http.getStream(), metaint);
    sink.reset();
    player.end();
    player.begin(icy, sink);
    const uint32_t start = millis();
    while (player.loop() && millis() - start < 60000u) { }
    Serial1.print("rate="); Serial1.println(player.sampleRate());
    Serial1.print("sink_frames="); Serial1.println(sink.frames());
    Serial1.print("pcm_fnv="); Serial1.println(sink.fnv());
    Serial1.print("decode_errors="); Serial1.println(player.decodeErrors());
    Serial1.print("metaint="); Serial1.println(metaint);
    Serial1.print("title="); Serial1.println(icy.title());
    player.end();
    http.end();

  } else if (!strcmp(cmd, "cabegin")) {
    ca_len = 0;
    Serial1.println("ca_reset=1");

  } else if (!strncmp(cmd, "caline ", 7)) {
    const char *p = cmd + 7;
    const size_t n = strlen(p);
    if (ca_len + n + 2 < sizeof(ca_pem)) {
      memcpy(ca_pem + ca_len, p, n);
      ca_len += n;
      ca_pem[ca_len++] = '\n';
    }
    Serial1.print("ca_bytes="); Serial1.println((uint32_t)ca_len);

  } else if (!strcmp(cmd, "caend")) {
    ca_pem[ca_len] = '\0';
    Serial1.print("ca_bytes="); Serial1.println((uint32_t)ca_len);

  } else if (!strncmp(cmd, "rtcset ", 7)) {
    /* Without this, TLS rejects every certificate: mbedtls compares the
       validity window against the clock, and an unset clock reads as the year
       2000. See docs/hazards.md. */
    ch32h4_rtc_begin(CH32H4_RTC_SRC_LSE);
    struct timeval tv = { (time_t)strtoul(cmd + 7, nullptr, 10), 0 };
    Serial1.print("rtc_set=");
    Serial1.println(settimeofday(&tv, nullptr) == 0 ? 1 : 0);

  } else if (!strncmp(cmd, "netplays ", 9)) {
    /* The same as netplay, over TLS, verifying against the uploaded CA rather
       than with setInsecure() -- a real station needs verification, and this
       is the path that also proves the clock is set. */
    static EthernetClientSecure sclient;
    static HTTPClient shttp;
    sclient.setHandshakeTimeout(25000);
    if (ca_len) {
      ca_pem[ca_len] = '\0';
      sclient.setCACert(ca_pem);
    } else {
      sclient.setInsecure();
    }
    if (!shttp.begin(sclient, String(cmd + 9))) {
      Serial1.println("http_rc=-1000"); Serial1.print("> "); return;
    }
    static const char *skeys[] = { "icy-metaint" };
    shttp.collectHeaders(skeys, 1);
    const int src = shttp.GET();
    Serial1.print("http_rc="); Serial1.println(src);
    if (src != 200) { shttp.end(); Serial1.print("> "); return; }

    const uint32_t smetaint = (uint32_t)shttp.header("icy-metaint").toInt();
    IcyStream sicy(shttp.getStream(), smetaint);
    sink.reset();
    player.end();
    player.begin(sicy, sink);
    const uint32_t sstart = millis();
    while (player.loop() && millis() - sstart < 60000u) { }
    Serial1.print("pcm_fnv="); Serial1.println(sink.fnv());
    Serial1.print("sink_frames="); Serial1.println(sink.frames());
    Serial1.print("decode_errors="); Serial1.println(player.decodeErrors());
    player.end();
    shttp.end();

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

  Ethernet.begin();
  const uint32_t deadline = millis() + 20000;
  while (!Ethernet.connected() && millis() < deadline) {
    delay(100);
  }

  Serial1.println();
  Serial1.println("mp3audio test");
  Serial1.print("net_ip=");
  Serial1.println(Ethernet.localIP());
  Serial1.print("net_up=");
  Serial1.println(Ethernet.connected() ? 1 : 0);
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
