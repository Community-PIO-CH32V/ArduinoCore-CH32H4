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

    /* 0.0 to 1.0, applied to every sample before it reaches the sink.
     *
     * Here rather than in the sketch because the writes happen inside the
     * player: scaling outside would mean wrapping the sink. Values above 1.0
     * are clamped, because a decoded MP3 already uses the full range and
     * amplifying it clips rather than getting louder. */
    void setVolume(float v);
    float volume() const { return (float)_volQ8 / 256.0f; }

    /* The same knob in whole percent, clamped to 0-100.
     *
     * Integer-exact rather than going through the float form: 256 is unity, so
     * the scale is percent * 256 / 100 and the multiply-and-shift in the hot
     * path stays a multiply and a shift. Two digits reach 99%, which is about
     * a tenth of a decibel below full scale -- close enough that a third digit
     * buys nothing. */
    void setVolumePercent(uint8_t percent);
    uint8_t volumePercent() const { return (uint8_t)((_volQ8 * 100u + 128u) / 256u); }

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
    /* Q8 fixed point: the scale is applied per sample in the hot path, and an
       integer multiply and shift is cheaper than a float multiply there. */
    uint16_t _volQ8 = 256;
    uint32_t _startedRate = 0;
    bool _running = false;
    bool _primed = false;
};
