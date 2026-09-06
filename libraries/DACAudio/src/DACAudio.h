/* Streaming audio out of the two internal 12-bit DACs.
 *
 *     DACAudio dac;
 *     dac.begin(44100);
 *     dac.writeFrame(left, right);
 *
 * analogWrite() and ch32h4_dac_write() already drive these pins one sample at
 * a time, which is a control voltage. This is the streaming path: TIM6 paces
 * the conversions, DMA feeds them, and a sketch only keeps the ring topped up
 * rather than hitting a 22 microsecond deadline forever.
 *
 * THE PINS ARE FIXED: PA4 is DAC1 and left, PA5 is DAC2 and right. There is no
 * mux. Mono duplicates to both, so a mono source works with whichever pin
 * happens to be wired -- which does mean PA5 is not available as an
 * independent analogWrite output while this is running.
 *
 * DO NOT analogWrite() PA4 OR PA5 WHILE THIS IS RUNNING. The single-shot path
 * reconfigures the channel for immediate conversion, which takes the trigger
 * away from TIM6 and stops the stream. begin() and end() put that path back
 * into a clean state, so using the two in turn is fine; using them at once is
 * not, and nothing prevents it.
 *
 * THESE ARE RAW DAC PINS. There is no output buffer worth the name at audio
 * rates: feed them into a HIGH-IMPEDANCE input, with an RC low-pass on each to
 * remove the sample-rate staircase, and not into a speaker or a low-impedance
 * amplifier input directly. Without the filter the output is a 12-bit
 * staircase at the sample rate and sounds like it -- which is easily mistaken
 * for a fault in this driver.
 *
 * THE DMA NEVER STOPS while running. It circles the ring whether or not
 * anything has been queued, so what it plays when a producer falls behind is
 * whatever is in the ring. The unwritten part is kept filled with silence, so
 * that is heard as a gap rather than as the last 93 ms repeating.
 */
#pragma once

#include <Arduino.h>
#include <AudioSink.h>

class DACAudio : public AudioSink {
public:
    /* Frames, not bytes: one frame is four bytes, both channels. 4096 is
       93 ms at 44.1 kHz, enough that a slow network read or a flash write
       cannot be heard. */
    static const size_t DEFAULT_FRAMES = 4096;
    static const size_t MIN_FRAMES = 256;
    static const uint32_t RATE_MIN = 1000;
    static const uint32_t RATE_MAX = 192000;

    DACAudio() { }
    ~DACAudio() { end(); }

    /* Before begin(); ignored after. False if below MIN_FRAMES. */
    bool setBuffer(size_t frames);

    /* Mono duplicates each sample to both pins. Changeable while running. */
    bool setStereo(bool stereo = true);
    bool setMono(bool mono = true) { return setStereo(!mono); }

    bool begin(uint32_t sampleRate) override;
    bool end() override;
    bool running() const override { return _running; }

    /* Exact, unlike I2S's: the timer divides an integer count off HCLK, and
       begin() refuses a rate it cannot produce closely. */
    uint32_t sampleRate() const override { return _rate; }

    size_t writeFrame(int16_t left, int16_t right) override;
    size_t writeFrames(const int16_t *interleaved, size_t frames) override;
    size_t availableFrames() override;
    uint32_t underruns() const override { return _underruns; }

    /* The ring index the DMA is reading. Exposed because it is the only honest
       way to measure the real sample clock: the DMA consumes exactly one word
       per conversion, so watching this advance over a known interval measures
       the rate the hardware is actually running at. */
    size_t dmaPosition() const;

    size_t bufferFrames() const { return _frames; }

    /* The ring itself, for diagnostics. Comparing what the CPU sees here
       against what the DAC is converting is the only way to tell a ring that
       was filled wrongly from one the DMA cannot read. */
    const uint32_t *buffer() const { return _ring; }

private:
    size_t freeFrames() const;
    void blankFree();
    bool startTimer(uint32_t rate);
    void syncWriter();

    uint32_t *_ring = nullptr;
    size_t _frames = DEFAULT_FRAMES;
    size_t _wr = 0;
    /* Silence is guaranteed from here forward to the read pointer; see
       blankFree(). */
    size_t _blankedTo = 0;
    uint32_t _rate = 0;
    uint32_t _underruns = 0;
    bool _stereo = true;
    bool _running = false;

    /* For spotting a drained ring between two writes; see syncWriter(). */
    bool _primed = false;
    size_t _queuedAtWrite = 0;
    uint32_t _lastWriteUs = 0;
};
