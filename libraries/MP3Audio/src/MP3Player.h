/* Pulls MP3 from a Stream, decodes it, and writes frames to an AudioSink.
 *
 *     static MP3Player player;        // see the note on size below
 *     player.begin(stream, i2s);
 *     void loop() { player.loop(); }
 *
 * The ends are deliberately interfaces: an HTTP socket, a File on SD and an
 * array in flash are all Streams, and I2S and DACAudio are both AudioSinks.
 * Playing from somewhere else is a different argument, not different code.
 *
 * loop() is non-blocking and does at most one frame of work per call. It does
 * NOT reconnect a dropped source -- that policy belongs to the sketch, where
 * it can be seen and changed, rather than buried here where it can be
 * neither tested nor overridden.
 *
 * DECLARE IT static OR GLOBAL, NOT ON THE STACK. It carries a 32 KB ring and
 * a 9 KB stereo scratch buffer as members, which is more than any default
 * stack on this part. One of these as a local will overflow into whatever is
 * next, and the resulting hard fault will look unrelated to audio.
 */
#pragma once

#include <Arduino.h>
#include <AudioSink.h>

#include "MP3Decoder.h"

class MP3Player {
public:
    static const size_t RING_BYTES = 32u * 1024u;
    /* Output waits for this much before starting. Without it the first second
       underruns every time, because decoding begins the instant one frame has
       arrived and then outruns a 16 KB/s source. */
    static const size_t PRIME_BYTES = 16u * 1024u;

    bool begin(Stream &source, AudioSink &sink);
    void end();

    /* False once the source is done and the ring is drained. */
    bool loop();

    bool running() const { return _running; }
    uint32_t sampleRate() const { return _decoder.sampleRate(); }
    size_t buffered() const { return _fill; }
    /* Straight through from the sink, which already counts correctly. This is
       the number that says whether the network or the CPU kept up. */
    uint32_t underruns() const { return _sink ? _sink->underruns() : 0; }
    uint32_t decodeErrors() const { return _decoder.errors(); }
    uint32_t rateChanges() const { return _rateChanges; }

private:
    size_t fillRing();
    bool decodeOne();

    Stream *_src = nullptr;
    AudioSink *_sink = nullptr;
    MP3Decoder _decoder;
    uint8_t _ring[RING_BYTES];
    size_t _fill = 0;              /* bytes valid, always from _ring[0] */
    int16_t _pcm[MP3Decoder::MAX_SAMPLES];
    int16_t _stereo[MP3Decoder::MAX_SAMPLES * 2];
    uint32_t _rateChanges = 0;
    uint32_t _startedRate = 0;
    bool _running = false;
    bool _primed = false;
};
