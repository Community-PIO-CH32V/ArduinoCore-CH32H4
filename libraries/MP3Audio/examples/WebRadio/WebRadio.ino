/*
   WebRadio - an MP3 internet radio station, over HTTPS, out of the I2S port.

   QUIET ON PURPOSE. It starts at 10%, because this is the first thing anyone
   runs and the first thing anyone runs should not be at full volume into
   whatever happens to be connected.

   TYPE TWO DIGITS ON THE SERIAL CONSOLE TO CHANGE IT. "05" is 5%, "15" is
   15%, "99" is as loud as two digits go -- about a tenth of a decibel below
   full scale, which is close enough that a third digit buys nothing. "00"
   mutes. No newline needed; the pair applies as soon as the second digit
   arrives.

   The change is heard a moment after it is typed, because the output buffer
   already holds audio at the old volume. That buffer is what makes a stalled
   connection inaudible, so it is not worth shortening to make the knob feel
   quicker.

   WIRING. I2S1: BCLK on PB12, WS follows it in hardware, data on PB15. Feed a
   proper I2S DAC or amplifier board -- a PCM5102, a MAX98357 and so on. These
   are digital pins and will not drive a speaker directly.

   Ethernet, not WiFi: this part has a MAC and this core has lwIP. Plug the
   RJ45 in before powering up.

   THE CLOCK COMES FROM NTP, AND IT HAS TO. TLS compares a certificate's
   validity window against the clock, and an unset RTC on this part reads as
   the year 2000 -- so every certificate looks "not yet valid" and every
   connection fails verification with no hint that the time is the problem.

   Two steps, because they are two different things. ch32h4_rtc_begin() starts
   the counter, or adopts one already running: the RTC lives in the backup
   domain, so after a warm reset it is still going and keeps the time it had.
   NTP then tells it what that time actually is. Without the first step the
   second silently does nothing -- ch32h4_rtc_set() refuses when no source is
   running, and NTP goes through settimeofday(), which is the same path.

   POINT IT AT WHATEVER THE STATION PUBLISHES. Playlists are resolved here, so
   a .m3u, .pls, .asx or Windows Media [Reference] link works as well as a
   direct stream, and chains of them work too -- one station reaches its audio
   through http, then https, then an .asx. http:// and https:// are both fine.

   What will NOT work is a web page that embeds a player: that is HTML, and
   HTML decoded as MP3 is noise. If a link plays in a browser it is not
   necessarily a stream.

   This example code is in the public domain.
*/

#include <LwipEthernet.h>
#include <NTP.h>
#include <RadioStream.h>
#include <I2S.h>
#include <MP3Player.h>

extern "C" {
#include "ch32h4_rtc.h"
}

/* ---- what to play, and how loudly ---------------------------------------- */

/* A direct stream, or any playlist that points at one. */
static const char *STREAM_URL = "http://play.antenne.de/antenne.m3u";

/* Percent of full scale. See the note at the top; two digits on the console
   change it while playing. */
static const uint8_t START_VOLUME_PCT = 10;

/* Empty uses whatever DHCP offered, which on most networks is something.
   Name it explicitly if your router offers no NTP option. */
static const char *NTP_SERVER = "pool.ntp.org";

/* THE ROOT YOUR STATION CHAINS TO. This core ships no CA bundle -- 140-odd
   roots is more RAM than mbedTLS can spend parsing them here -- so verifying
   means naming the one you need.

   ISRG Root X1 is Let's Encrypt's and covers a large share of stations.
   To find out which root a particular one uses, and get its PEM:

     openssl s_client -showcerts -connect stream.example.org:443 < /dev/null |
       openssl x509 -noout -issuer

   That names the issuer of the server's certificate. Servers usually send
   their intermediates but NOT the root, so fetch the root itself from the CA's
   own site rather than from the connection -- a root taken from the peer
   proves nothing, since the peer is what you are trying to verify.

   Left as a placeholder deliberately: this example fails closed rather than
   appearing to verify something it does not. */
static const char root_ca[] =
  "-----BEGIN CERTIFICATE-----\n"
  "...paste the root certificate here...\n"
  "-----END CERTIFICATE-----\n";

/* Set true to run without verification while bringing a board up. The
   connection is then encrypted against a passive listener and offers nothing
   at all against anyone who can answer for the station's address. */
static const bool INSECURE = false;

/* ---- the pipeline -------------------------------------------------------- */

/* STATIC, NOT LOCAL. MP3Player carries a 32 KB ring and a 9 KB scratch buffer
   as members, which is far more than the default stack. One of these declared
   inside a function overflows into whatever is next, and the hard fault that
   follows looks nothing to do with audio. */
static I2S i2s(OUTPUT, 0);
static MP3Player player;

static RadioStream radio;

static uint32_t lastReport = 0;
static uint32_t lastTitleChanges = 0;

/* Start the RTC, then learn the time. False if we still do not know it, in
   which case TLS will reject every certificate and saying so here is far
   kinder than letting it fail as a verification error. */
static bool startClock() {
  /* LSE is the crystal at PC14/PC15 and the only source that survives on
     VBAT alone. If this board has no crystal, CH32H4_RTC_SRC_HSE is accurate
     while powered; LSI needs nothing but drifts minutes a day. */
  if (!ch32h4_rtc_begin(CH32H4_RTC_SRC_LSE)) {
    Serial.println("no 32 kHz crystal -- falling back to HSE");
    if (!ch32h4_rtc_begin(CH32H4_RTC_SRC_HSE)) {
      Serial.println("could not start the RTC at all");
      return false;
    }
  }

  NTP.begin(NTP_SERVER);
  /* Returns true immediately if the clock was already right -- which after a
     warm reset it usually is, because the counter kept running. */
  if (!NTP.waitSynced(15000)) {
    Serial.println("NTP did not answer; the clock is unknown, so TLS will");
    Serial.println("reject every certificate. Check DNS and the route out.");
    return false;
  }

  time_t now = time(nullptr);
  Serial.print("clock set: ");
  Serial.print(ctime(&now));            /* ctime() supplies the newline */
  return true;
}

/* Open the stream and start the player. False if anything failed, in which
   case loop() waits and tries again.

   RadioStream does the URL work: redirects, playlists and chains of them, and
   Shoutcast metadata stripping. It lives in the library rather than here
   because that loop has real edge cases and logic in an example cannot be
   tested. */
static bool connectStream() {
  if (INSECURE) {
    radio.setInsecure();
  } else {
    radio.setCACert(root_ca);
  }

  /* SAY SO BEFORE FAILING, because the failure that follows looks like a
     network problem and is not.
     An https station with the placeholder root cannot verify anything, so the
     handshake never completes and GET reports -1, which is
     HTTPC_ERROR_CONNECTION_FAILED -- the same code a wrong address gives. */
  const bool https = strncmp(STREAM_URL, "https://", 8) == 0;
  if (https && !INSECURE && strstr(root_ca, "paste the root") != nullptr) {
    Serial.println("root_ca is still the placeholder, so this https station");
    Serial.println("cannot be verified and the connection will fail with -1.");
    Serial.println("Paste the station's root certificate into root_ca, or set");
    Serial.println("INSECURE = true to connect without verifying it.");
  }

  if (!radio.begin(STREAM_URL, &Serial)) { return false; }

  player.setVolumePercent(START_VOLUME_PCT);
  if (!player.begin(radio.stream(), i2s)) {
    Serial.println("player.begin() failed");
    radio.end();
    return false;
  }
  lastTitleChanges = 0;
  return true;
}

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { }

  Serial.println();
  Serial.println("WebRadio");

  i2s.setBCLK(PB12);
  i2s.setDATA(PB15);
  i2s.setBitsPerSample(16);
  /* THE OUTPUT BUFFER, and worth setting for a network stream.
     The default is 4096 bytes, which at 44.1 kHz 16-bit stereo is 23 ms of
     audio -- every pause in the network longer than that is an audible gap.
     32 KB is 186 ms, which rides out a slow segment, and this part has RAM to
     spare. A frame of MP3 is 4608 bytes of PCM on its own. */
  i2s.setBuffer(32768);
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

  /* AFTER the link is up, because NTP needs the network -- and before the
     stream, because TLS needs the clock. */
  startClock();

  connectStream();
}

/* Two digits set the volume, applied on the second.
 *
 * ANY NON-DIGIT CLEARS THE PAIR, which is not fussiness: a terminal that
 * sends a newline, or one mistyped letter, would otherwise pair with the next
 * digit typed and shift every command after it by one. */
static void pollVolume() {
  static char digits[2];
  static uint8_t n = 0;

  while (Serial.available()) {
    const int c = Serial.read();
    if (c >= '0' && c <= '9') {
      digits[n++] = (char)c;
      if (n == 2) {
        const uint8_t pct = (uint8_t)((digits[0] - '0') * 10 + (digits[1] - '0'));
        player.setVolumePercent(pct);
        n = 0;
        Serial.print("volume ");
        Serial.print(player.volumePercent());
        Serial.println("%");
      }
    } else {
      n = 0;
    }
  }
}

void loop() {
  pollVolume();

  /* One frame of work per call, non-blocking. */
  if (!player.loop()) {
    /* RECONNECTING IS THIS SKETCH'S JOB, not the library's. A retry policy
       buried in MP3Player could be neither tested nor overridden, and every
       application wants a different one.
       The message distinguishes the two cases, because "stream ended" while
       retrying a connection that never opened sends you looking for the wrong
       problem. */
    Serial.println(radio.connected() ? "stream ended, reconnecting in 2 s"
                                     : "not connected, retrying in 2 s");
    player.end();
    radio.end();
    delay(2000);
    connectStream();
    return;
  }

  if (radio.titleChanges() != lastTitleChanges) {
    lastTitleChanges = radio.titleChanges();
    Serial.print("now playing: ");
    Serial.println(radio.title());
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
    Serial.print(player.decodeErrors());
    Serial.print(", volume ");
    Serial.print(player.volumePercent());
    Serial.println("%");
  }
}
