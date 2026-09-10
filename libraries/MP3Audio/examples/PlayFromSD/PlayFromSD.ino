/*
   PlayFromSD - the same player, fed by a file instead of a socket.

   THE POINT OF THIS EXAMPLE is that it is the same MP3Player as WebRadio with
   one argument changed. The player takes a Stream; a File is a Stream, an
   HTTP response body is a Stream, and an array in flash can be one. Nothing
   about decoding or output knows or cares which it got.

   It is also where to debug decoding with no network in the way. If a file
   plays here and a station does not, the problem is the stream or the
   connection, not the decoder -- which is a much smaller place to look.

   QUIET ON PURPOSE. VOLUME is a tenth of full scale, for the same reason as
   in WebRadio: this may be the first thing you run.

   WIRING. I2S1: BCLK on PB12, WS follows in hardware, data on PB15, into a
   proper I2S DAC or amplifier board. SD in 1-bit mode: MISO PC8, CLK PC12,
   MOSI PD2, CS PC11. Put an MP3 at /track.mp3 in the card's root.

   This example code is in the public domain.
*/

#include <SPI.h>
#include <SD.h>
#include <I2S.h>
#include <MP3Player.h>

static const char *TRACK = "/track.mp3";
static const float VOLUME = 0.1f;   /* see above */

static const int SD_CS = PC11;

/* STATIC, NOT LOCAL: MP3Player carries a 32 KB ring as a member, which is far
   more than the default stack. */
static I2S i2s(OUTPUT, 0);
static MP3Player player;
static File track;

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) { }

  Serial.println();
  Serial.println("PlayFromSD");

  if (!SD.begin(SD_CS)) {
    Serial.println("no card");
    return;
  }

  track = SD.open(TRACK, FILE_READ);
  if (!track) {
    Serial.print("cannot open ");
    Serial.println(TRACK);
    return;
  }
  Serial.print("playing ");
  Serial.print(TRACK);
  Serial.print(", ");
  Serial.print(track.size());
  Serial.println(" bytes");

  i2s.setBCLK(PB12);
  i2s.setDATA(PB15);
  i2s.setBitsPerSample(16);
  /* No begin(): the rate comes from the first decoded frame, and the player
     starts the sink itself once it knows it. */

  player.setVolume(VOLUME);
  player.begin(track, i2s);
}

void loop() {
  if (!player.loop()) {
    Serial.print("done -- ");
    Serial.print(player.decodeErrors());
    Serial.print(" decode errors, ");
    Serial.print(player.underruns());
    Serial.println(" underruns");
    player.end();
    track.close();
    while (true) { delay(1000); }
  }
}
