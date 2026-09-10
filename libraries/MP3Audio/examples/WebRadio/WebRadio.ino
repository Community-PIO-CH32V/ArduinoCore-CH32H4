/*
   WebRadio - an MP3 internet radio station, over HTTPS, out of the I2S port.

   QUIET ON PURPOSE. VOLUME is a tenth of full scale, because this is the
   first thing anyone runs and the first thing anyone runs should not be at
   full volume into whatever happens to be connected. Raise it once you know
   what is on the other end.

   WIRING. I2S1: BCLK on PB12, WS follows it in hardware, data on PB15. Feed a
   proper I2S DAC or amplifier board -- a PCM5102, a MAX98357 and so on. These
   are digital pins and will not drive a speaker directly.

   Ethernet, not WiFi: this part has a MAC and this core has lwIP. Plug the
   RJ45 in before powering up.

   SET THE CLOCK, AND KNOW WHY. TLS compares a certificate's validity window
   against the clock, and an unset RTC on this part reads as the year 2000 --
   so every certificate looks "not yet valid" and every connection fails
   verification with no hint that the time is the problem. This sketch sets the
   RTC from a build-time constant below, which is crude but honest. A real
   product would use NTP or a battery-backed RTC.

   FINDING A STREAM URL. Most stations publish a .pls or .m3u playlist; open it
   in a text editor and take the http:// or https:// line inside. A URL that
   plays in a browser is not always a stream -- a page that embeds a player is
   HTML, and this sketch will decode HTML as MP3 and produce noise.

   This example code is in the public domain.
*/

#include <LwipEthernet.h>
#include <EthernetClientSecure.h>
#include <HTTPClient.h>
#include <I2S.h>
#include <IcyStream.h>
#include <MP3Player.h>

extern "C" {
#include "ch32h4_rtc.h"
}

/* ---- what to play, and how loudly ---------------------------------------- */

static const char *STREAM_URL = "https://example.org/stream.mp3";

/* A tenth of full scale. See the note at the top. */
static const float VOLUME = 0.1f;

/* Seconds since 1970, only so TLS can check a certificate. Any roughly
   correct value works; being a few days out is fine, being 26 years out is
   not. Update it or replace this with NTP. */
static const uint32_t BUILD_EPOCH = 1789000000u;

/* Verification is on. To try a station whose CA this core does not carry,
   swap setCACert() for setInsecure() below -- and understand that this stops
   checking who you are talking to. */

/* ---- the pipeline -------------------------------------------------------- */

/* STATIC, NOT LOCAL. MP3Player carries a 32 KB ring and a 9 KB scratch buffer
   as members, which is far more than the default stack. One of these declared
   inside a function overflows into whatever is next, and the hard fault that
   follows looks nothing to do with audio. */
static I2S i2s(OUTPUT, 0);
static MP3Player player;

static EthernetClient plain;
static EthernetClientSecure secure;
static HTTPClient http;

static uint32_t lastReport = 0;
static uint32_t lastTitleChanges = 0;
static IcyStream *icy = nullptr;

static void setClock() {
  ch32h4_rtc_begin(CH32H4_RTC_SRC_LSE);
  struct timeval tv = { (time_t)BUILD_EPOCH, 0 };
  settimeofday(&tv, nullptr);
}

/* Open the stream and start the player. False if anything failed, in which
   case loop() waits and tries again. */
static bool connectStream() {
  const bool https = strncmp(STREAM_URL, "https:", 6) == 0;

  if (https) {
    secure.setHandshakeTimeout(20000);
    secure.setInsecure();       /* see the note above; use setCACert() in anger */
    if (!http.begin(secure, STREAM_URL)) { return false; }
  } else {
    if (!http.begin(plain, STREAM_URL)) { return false; }
  }

  /* BEFORE GET(). HTTPClient keeps only the headers it is told to keep, and a
     metaint that silently reads as zero turns Shoutcast's metadata blocks into
     corrupt audio -- a glitch every few seconds that sounds like a broken
     decoder. */
  static const char *keys[] = { "icy-metaint" };
  http.collectHeaders(keys, 1);

  const int rc = http.GET();
  if (rc != 200) {
    Serial.print("GET failed: ");
    Serial.println(rc);
    http.end();
    return false;
  }

  const uint32_t metaint = (uint32_t)http.header("icy-metaint").toInt();
  Serial.print("connected, icy-metaint=");
  Serial.println(metaint);

  delete icy;
  icy = new IcyStream(http.getStream(), metaint);
  lastTitleChanges = 0;

  player.setVolume(VOLUME);
  if (!player.begin(*icy, i2s)) {
    Serial.println("player.begin() failed");
    http.end();
    return false;
  }
  return true;
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { }

  Serial.println();
  Serial.println("WebRadio");

  setClock();

  i2s.setBCLK(PB12);
  i2s.setDATA(PB15);
  i2s.setBitsPerSample(16);
  /* No begin() here: the sample rate is not known until the first MP3 frame
     decodes, so MP3Player starts the sink itself on that frame. */

  Ethernet.begin();
  const uint32_t deadline = millis() + 20000;
  while (!Ethernet.connected() && millis() < deadline) { delay(100); }
  if (!Ethernet.connected()) {
    Serial.println("no link -- is the cable in?");
    return;
  }
  Serial.print("ip ");
  Serial.println(Ethernet.localIP());

  connectStream();
}

void loop() {
  /* One frame of work per call, non-blocking. */
  if (!player.loop()) {
    /* RECONNECTING IS THIS SKETCH'S JOB, not the library's. A retry policy
       buried in MP3Player could be neither tested nor overridden, and every
       application wants a different one. */
    Serial.println("stream ended, reconnecting in 2 s");
    player.end();
    http.end();
    delay(2000);
    connectStream();
    return;
  }

  if (icy && icy->titleChanges() != lastTitleChanges) {
    lastTitleChanges = icy->titleChanges();
    Serial.print("now playing: ");
    Serial.println(icy->title());
  }

  /* underruns() is the number that matters: it says the network or the CPU
     failed to keep the output fed, which is what a listener hears as a gap. */
  if (millis() - lastReport > 5000) {
    lastReport = millis();
    Serial.print("rate ");
    Serial.print(player.sampleRate());
    Serial.print(" Hz, buffered ");
    Serial.print(player.buffered());
    Serial.print(" B, underruns ");
    Serial.print(player.underruns());
    Serial.print(", decode errors ");
    Serial.println(player.decodeErrors());
  }
}
