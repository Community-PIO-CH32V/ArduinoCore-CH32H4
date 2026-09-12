# Streaming audio out of the DACs, behind one sink interface

**Status:** design, approved 2026-09-06.

## What this is for

The part has two 12-bit DACs on fixed pins, PA4 and PA5. `analogWrite()` and
`ch32h4_dac_write()` already drive them one sample at a time, which is fine for
a control voltage and useless for audio: at 44.1 kHz a sketch would have to hit
a 22 microsecond deadline from `loop()`, forever.

This adds the streaming path — a timer paces the conversions, DMA feeds them,
and a sketch only has to keep a ring topped up — and puts it behind an
interface the existing I2S library also implements, so that the eventual
ESP8266Audio port writes one adapter rather than one per output.

```cpp
DACAudio dac;
dac.begin(44100);
dac.writeFrame(left, right);
```

## What already exists

- `cores/ch32h4/ch32h4_dac.{c,h}`: single-shot writes, channel/pin mapping,
  output-buffer control. Unchanged by this work and reused for pin setup.
- `libraries/I2S`: a working, tested `Stream` with ping-pong DMA on DMA1
  channels 4 and 5, `write(int16_t left, int16_t right)`, and mono duplication
  already implemented. It gains an interface, not a rewrite.
- `cores/ch32h4/ch32h4_timer.c`: the timer table, including TIM6.
- `variants/CH32H417QEU6/pins_package.h`: `PIN_DAC1` = PA4, `PIN_DAC2` = PA5,
  fixed, no mux.

**Resources are free and were checked, not assumed.** TIM6 is unused (Tone,
`analogWrite` and Servo use TIM1, TIM2 and TIM8). DMA1 channel 1 is unused
(SPI has 2 and 3, I2S has 4 and 5, ADCInput has 7).

## The prior art this is ported from

MicroPython's CH32 port has `machine_audioout.c`, a 504-line driver doing
exactly this, and it is the source for the hardware chain and for three details
below that each represent a bug somebody already had. This is a port with an
Arduino-shaped API on top, not a fresh derivation.

## The hardware chain

```
TIM6 update -> TRGO -> both DAC channels trigger together
                    -> DMA request 103 (DAC1) via the DMAMUX
                    -> one 32-bit word from the ring into DAC->RD12BDHR
```

`RD12BDHR` is the dual 12-bit right-aligned register: channel 1 in bits 11:0,
channel 2 in bits 27:16. **A stereo frame is therefore one 32-bit write and one
DMA transfer**, and the two channels cannot drift apart — which is the whole
reason to use it rather than a DMA channel per side. DAC2's own DMA request
(104) goes unused.

The DMA runs circular and never stops while the sink is running. It plays
whatever is in the ring, fed or not.

## `AudioSink`

A frame-oriented interface in `cores/ch32h4/AudioSink.h`, header-only,
implemented by `DACAudio` and by `I2S`.

```cpp
class AudioSink {
public:
    virtual ~AudioSink() { }

    virtual bool begin(uint32_t sampleRate) = 0;
    virtual void end() = 0;
    virtual bool running() const = 0;
    virtual uint32_t sampleRate() const = 0;

    /* One stereo frame. Mono duplicates to both channels. Returns 1 if the
       frame was queued, 0 if there was no room. */
    virtual size_t writeFrame(int16_t left, int16_t right) = 0;

    /* Interleaved stereo frames. Returns how many were accepted, which may be
       fewer than offered. */
    virtual size_t writeFrames(const int16_t *interleaved, size_t frames) = 0;

    /* Frames that can be accepted without blocking. */
    virtual size_t availableFrames() = 0;

    /* Frames the hardware wanted and did not have. Zero until something has
       been queued: an empty ring before the first write is normal. */
    virtual uint32_t underruns() const = 0;
};
```

**This is deliberately not ESP8266Audio's `AudioOutput`.** That interface is
byte-and-state shaped and carries choices — `SetGain`, `SetBitsPerSample`,
`SetChannels` — whose only consumer does not exist in this core yet. Fixing an
interface before its consumer arrives is how it ends up wrong. `AudioSink` says
only what both sinks genuinely share today; the ESP8266Audio port adds an
adapter over it, once, for both outputs.

Gain is not here either. Scaling samples belongs to whatever produces them; a
sink that also attenuates invites two volume controls in series.

## `DACAudio`

`libraries/DACAudio/src/DACAudio.{h,cpp}`. Its own library, so a sketch that
never plays audio links none of it.

```cpp
class DACAudio : public AudioSink {
public:
    bool begin(uint32_t sampleRate) override;   // 1000 .. 192000
    void end() override;

    bool setBuffer(size_t frames);              // before begin(); min 256
    bool setStereo(bool stereo = true);
    bool setMono(bool mono = true) { return setStereo(!mono); }

    size_t writeFrame(int16_t left, int16_t right) override;
    size_t writeFrames(const int16_t *interleaved, size_t frames) override;
    size_t availableFrames() override;
    uint32_t underruns() const override;
    bool running() const override;
    uint32_t sampleRate() const override;
};
```

**Sample format.** Signed 16-bit in, unsigned 12-bit out: `(s + 32768) >> 4`,
packed as `left | (right << 16)`.

**Mono duplicates to both pins.** It costs nothing — the dual register is
written either way — and a mono source then works with whichever pin is wired
and with a stereo amplifier. PA5 is not left free for `analogWrite`; a sketch
wanting an independent DC output alongside audio does not have one.

**Buffer.** 4096 frames by default: 16 KB, and 93 ms at 44.1 kHz, which is
enough that a slow network read or a flash write cannot be heard. `setBuffer()`
takes frames, with a floor of 256. The ring is allocated in `begin()` and freed
in `end()`.

**Three details kept verbatim from the MicroPython driver**, each of which is a
bug already found once:

1. **Idle and starved output sits at mid-scale**, `0x08000800`. A DAC resting
   at zero drives an amplifier hard against one rail.
2. **The ring ahead of the write pointer is filled with silence.** The DMA
   circles whether or not anything is queued, so what it plays when the
   producer falls behind is whatever was there — which, left alone, is the
   previous pass's audio, heard as the last fraction of a second repeating.
3. **Underruns are counted rather than merely survived**, and only after the
   first write, so that the normally-empty ring before playback starts is not
   reported as a fault.

**These are raw DAC pins.** No output buffer worth the name at audio rates:
feed them into a high-impedance input, with an RC low-pass on each to remove
the sample-rate staircase before it reaches an amplifier. The header says so,
because the alternative is someone concluding the driver is noisy.

## What changes in I2S

Additive only: `class I2S : public Stream, public AudioSink`, plus forwarding
methods over machinery it already has. **No existing method changes signature
or behaviour** — sketches use `I2S` as a `Stream` and that must keep working.

Three details the forwarding cannot gloss over, each checked against the code
rather than assumed:

**A frame is not always 4 bytes.** `setBitsPerSample()` accepts 16 or 32, so a
stereo frame is `2 * (bits / 8)` bytes — 4 or 8. `availableFrames()` is
therefore `availableForWrite() / frameBytes()`, since `availableForWrite()`
returns bytes.

**`AudioSink` frames are 16-bit signed**, because that is what decoders produce
and what the eventual ESP8266Audio adapter will hand over. In 32-bit mode I2S
widens by `sample << 16`. The interface does not grow a width parameter for a
choice only one of its two implementations has.

**`writeFrame()` must not block**, and `I2S::write()` does — up to the stream
timeout. So `writeFrame()` checks `availableForWrite()` first and returns 0
when there is no room, rather than delegating to the blocking path. A sink
that blocks is how a stream stutters.

The one genuine addition is `underruns()`. I2S does not currently count them;
it needs a counter incremented where the DMA interrupt finds the ring empty.

## Error handling

`begin()` returns false and changes nothing on: a rate outside 1 kHz–192 kHz, a
buffer allocation that fails, or being called twice without `end()`. There is
no error string — unlike `FatFS.begin()`, which has four distinct causes,
these are three and a sketch can tell them apart from what it asked for.

A `writeFrame()` with no room returns 0 rather than blocking. `writeFrames()`
returns a short count. Neither blocks, because a sink that blocks inside an
audio callback is how a stream stutters; a sketch that wants to wait can
`yield()` on `availableFrames()`.

## Testing

**Everything defaults to silence.** Tests write mid-scale or measure timing;
none produces audible content. There is an amplifier and a speaker wired to
I2S instance 0's pins on this bench, and the DAC's PA4/PA5 do not reach it —
but the rule holds regardless, and an audible check happens only on request and
at low volume.

**Static:** examples build under PlatformIO and arduino-cli.

**Hardware**, in `tests/hw/test_dacaudio.py` with its own sketch:

1. **The sample clock is the requested rate.** Read `DMA_GetCurrDataCounter`
   twice over a known interval and divide: the DMA consumes exactly one word
   per conversion, so this measures the true rate to a fraction of a percent.
   Checked at 8 kHz, 44.1 kHz and 96 kHz.
2. **`availableFrames()` tracks the ring.** Full after `begin()`, falls as
   frames are queued, recovers as the DMA drains.
3. **Underruns are zero when fed and non-zero when starved.** Both halves: a
   counter that only ever reads zero proves nothing.
4. **Mono writes reach both channels.** Read `DAC->RD12BDHR` back and confirm
   both halves hold the same code.
5. **`begin()` refuses** 999 Hz and 192001 Hz, and a second `begin()` without
   `end()`.
6. **`end()` releases the timer and DMA channel**, so a second `begin()`
   succeeds and TIM6 is left disabled.

## Out of scope

- ESP8266Audio itself, MP3 decoding, and HTTP/HTTPS streaming. This is the sink
  they will eventually write into.
- Gain and volume, which belong to the producer.
- DAC input (there is none) and I2S input, which already exists.
- Any change to `ch32h4_dac.{c,h}` or to `analogWrite`.

## Order of work

1. `AudioSink.h`, and I2S implementing it — including its new underrun
   counter, the frame-size arithmetic and the non-blocking `writeFrame()`.
   Landable alone: the existing I2S hardware tests are the proof that nothing
   broke, plus a new test that a 32-bit-mode frame is 8 bytes and that
   `writeFrame()` returns 0 rather than blocking on a full ring.
2. `DACAudio` with the timer, DMA and ring, tested against the sample-clock
   measurement before anything plays through it.
3. Silence, underrun counting and the starved-ring behaviour.
4. Examples, and the hardware test file.

Step 1 touches working code and nothing else depends on it; step 2 onward is
new code only.
