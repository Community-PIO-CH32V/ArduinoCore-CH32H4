/* One audio output, whichever pins it comes out of.
 *
 * Implemented by DACAudio (the two internal 12-bit DACs) and by I2S. It exists
 * so that a producer -- a tone generator, a WAV reader, eventually an
 * ESP8266Audio decoder -- can be written once against "somewhere to put
 * frames" rather than once per output.
 *
 * DELIBERATELY NOT ESP8266Audio's AudioOutput. That interface carries SetGain,
 * SetBitsPerSample and SetChannels, decisions whose only consumer does not
 * exist in this core yet, and fixing an interface before its consumer arrives
 * is how it ends up wrong. This says only what the two outputs here genuinely
 * share; the ESP8266Audio port adds an adapter over it, once.
 *
 * NO GAIN CONTROL, for the same reason a mixer has one fader per channel and
 * not two: scaling belongs to whatever produces the samples, and a sink that
 * also attenuates invites two volume controls in series and a quiet bug.
 *
 * FRAMES ARE 16-BIT SIGNED STEREO, interleaved. That is what decoders produce.
 * An implementation whose hardware wants something else converts internally --
 * I2S in 32-bit mode shifts left by 16, the DACs shift right by 4 -- rather
 * than the interface growing a width parameter for a choice only one of them
 * has.
 *
 * NOTHING HERE BLOCKS. A sink that waits inside a producer's callback is how a
 * stream stutters; a caller that wants to wait does so on availableFrames().
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

class AudioSink {
public:
    virtual ~AudioSink() { }

    /* Start the output at `sampleRate` Hz. False if the rate is out of range,
     * a resource is already held by something else, or it is already running.
     *
     * uint32_t RATHER THAN long, which is why I2S::begin() takes uint32_t. The
     * two cannot coexist: begin(44100) passes an int, which converts to long
     * and to uint32_t equally well, so declaring both makes every existing
     * call ambiguous and the library stops compiling. */
    virtual bool begin(uint32_t sampleRate) = 0;

    /* Stop, release the hardware, and leave the output at rest.
     *
     * RETURNS bool ONLY because I2S::end() already does and C++ will not
     * overload on return type -- a void end() here would collide with it, and
     * neither could be renamed without breaking sketches. Callers may ignore
     * it. */
    virtual bool end() = 0;

    virtual bool running() const = 0;

    /* The rate the output is ACTUALLY running at, which is not always the one
     * asked for: I2S divides by an integer with a single half-step, so 44100
     * comes back as something near it. A sketch generating a tone needs the
     * real number. */
    virtual uint32_t sampleRate() const = 0;

    /* One stereo frame. A mono sink duplicates. Returns 1 if it was queued,
     * 0 if there was no room -- this does not wait. */
    virtual size_t writeFrame(int16_t left, int16_t right) = 0;

    /* Interleaved stereo frames. Returns how many were accepted, which may be
     * fewer than offered and may be zero. */
    virtual size_t writeFrames(const int16_t *interleaved, size_t frames) = 0;

    /* Frames that can be accepted right now without waiting. */
    virtual size_t availableFrames() = 0;

    /* Frames the hardware wanted and did not have. Zero until something has
     * been queued, because an empty buffer before playback starts is normal
     * rather than a fault. */
    virtual uint32_t underruns() const = 0;
};
