/* An MP3 frame decoder, and nothing else.
 *
 * This class knows about libhelix and about bytes. It does not know where the
 * bytes came from or where the samples are going, which is what lets it be
 * tested against an array in flash with no I/O at all.
 *
 * The vendored decoder in libhelix-mp3/ is RealNetworks RPSL/RCSL licensed,
 * not MIT. See THIRD-PARTY.md.
 */
#pragma once

#include <Arduino.h>

class MP3Decoder {
public:
    /* 1152 samples per channel is the MP3 frame size and helix writes them
       interleaved, so a stereo frame is 2304 int16_t. */
    static const size_t MAX_SAMPLES = 2304;

    MP3Decoder() { }
    ~MP3Decoder() { end(); }

    bool begin();
    void end();

    /* Decode ONE frame starting at `in`.
     *
     *   >0  int16_t samples written to `pcm`
     *    0  more input needed; nothing consumed beyond any skipped bytes
     *   <0  a frame was dropped and resynced past; carry on
     *
     * `*consumed` is ALWAYS set, in every case, to how many input bytes were
     * taken -- including bytes skipped to reach a sync word. A caller that
     * advances by `*consumed` cannot desynchronise, which is what makes the
     * error path and the mid-stream join the same code. */
    int decodeFrame(const uint8_t *in, size_t inLen, size_t *consumed,
                    int16_t *pcm, size_t pcmCap);

    uint32_t sampleRate() const { return _rate; }
    uint8_t channels() const { return _channels; }
    uint32_t bitrate() const { return _bitrate; }
    uint32_t frames() const { return _frames; }
    uint32_t errors() const { return _errors; }

private:
    void *_dec = nullptr;          /* HMP3Decoder, kept opaque in the header */
    uint32_t _rate = 0;
    uint32_t _bitrate = 0;
    uint32_t _frames = 0;
    uint32_t _errors = 0;
    uint8_t _channels = 0;
};
