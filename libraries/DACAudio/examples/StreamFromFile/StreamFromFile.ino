/*
   StreamFromFile - play raw PCM from LittleFS out of the internal DACs.

   The shape every real audio source has: something slow produces samples, the
   ring absorbs the jitter, and the DACs never notice. A decoder goes exactly
   where the file read is here.

   THE FILE. Raw signed 16-bit little-endian stereo at 44.1 kHz, no header --
   the format writeFrames() takes. Convert with:

       sox in.wav -r 44100 -c 2 -b 16 -e signed -t raw audio.raw

   and upload it to /audio.raw with the filesystem uploader. A .wav file will
   play its 44-byte header as a click and then work, which is a good way to
   confirm you meant to use raw.

   VOLUME COMES FROM THE FILE. This example does not scale samples, so a
   full-scale recording plays at full scale. Check what is connected first, and
   see PlayTone for the wiring these pins need -- an RC low-pass into a
   high-impedance input, never a speaker directly.

   UNDERRUNS ARE INFORMATION, not a fault. A non-zero count at the end means
   the filesystem could not keep up with the sample rate; a bigger setBuffer()
   or a lower rate is the answer.

   This example code is in the public domain.

   ---
   For the CH32H41x core. Set a filesystem size first (board_build.filesystem_size,
   or Tools > Filesystem size); with none, begin() fails and says so.
*/

#include <DACAudio.h>
#include <LittleFS.h>

DACAudio dac;
File audio;

/* Frames per read. 256 frames is 1 KB, big enough that the per-read overhead
   does not dominate and small enough to stay well inside the ring. */
const size_t CHUNK = 256;
int16_t buf[CHUNK * 2];         /* interleaved: two samples per frame */

void setup() {
  Serial.begin(115200);
  while (!Serial && millis() < 3000) {
    delay(10);
  }

  if (!LittleFS.begin()) {
    Serial.println("LittleFS.begin() failed -- is a filesystem size set?");
    return;
  }
  audio = LittleFS.open("/audio.raw", "r");
  if (!audio) {
    Serial.println("no /audio.raw -- see the header for how to make one");
    return;
  }
  Serial.print("playing ");
  Serial.print(audio.size());
  Serial.println(" bytes");

  if (!dac.begin(44100)) {
    Serial.println("DACAudio.begin() failed");
    audio.close();
    return;
  }
}

void loop() {
  if (!dac.running() || !audio) {
    return;
  }

  /* Read only as much as the ring can take. Reading more would mean holding
     samples in a second buffer, which is what the ring is for. */
  size_t room = dac.availableFrames();
  if (room == 0) {
    return;                     /* full: come back next loop() */
  }
  if (room > CHUNK) {
    room = CHUNK;
  }

  const size_t want = room * 2 * sizeof(int16_t);
  const int got = audio.read((uint8_t *)buf, want);
  if (got <= 0) {
    Serial.print("done, underruns: ");
    Serial.println(dac.underruns());
    audio.close();
    dac.end();
    return;
  }
  /* A short read at the end of the file is normal; a partial frame is not, so
     round down rather than playing half a frame and swapping the channels for
     everything after it. */
  dac.writeFrames(buf, (size_t)got / (2 * sizeof(int16_t)));
}
