# MP3 web radio

**Status:** design, approved 2026-09-09.

## What this is for

Play an MP3 internet radio stream over HTTPS out of the I2S port. The board
has Ethernet, a TLS client, an HTTP client and two audio outputs already; what
is missing is an MP3 decoder and the plumbing between a socket and a speaker.

This is **not** a port of ESP8266Audio. That library carries roughly twenty
file sources, ten generators and eight outputs, almost none of which serves a
radio. What is taken from it is the decoder it vendors and the pump-from-loop
model it proves on much weaker silicon. Sketches written for ESP8266Audio will
not compile against this; that was a deliberate choice over an API-compatible
subset, because the compatible API assumes ESP-specific types and a
`loop()`-pumping contract that would have to be reproduced rather than
designed.

## What already exists

Nothing here needs building, and the design is shaped around it:

| Piece | Where | What it gives us |
|---|---|---|
| `AudioSink` | `cores/ch32h4/AudioSink.h` | the output interface, implemented by both `I2S` and `DACAudio` |
| `I2S` 1.1.0 | `libraries/I2S` | the speaker output, and `underruns()` that already counts correctly |
| `HTTPClient` | `libraries/HTTPClient` | `GET()`, `getStream()` returning a `Client&`, `collectHeaders()`/`header()` |
| `EthernetClientSecure` | `libraries/EthernetClientSecure` | TLS with `setCACert()` / `setInsecure()` |
| `ch32h4_rtc` | core | `rtcset`-style clock, without which TLS rejects every certificate |
| local HTTP/TLS test servers | `tests/hw/test_webserver.py`, `test_tls_server.py` | the pattern for a deterministic network test |

## Architecture

```
EthernetClientSecure -> HTTPClient -> Client& (the response body)
                                          |
                                     IcyStream            strips Shoutcast metadata
                                          |
                                     MP3Player            owns the ring, pumps
                                       |- 32 KB ring in DTCM
                                       |- MP3Decoder      wraps libhelix
                                       '- writeFrames() -> AudioSink -> I2S
```

Four units in one new library, `libraries/MP3Audio`.

### `MP3Decoder`

The only thing that knows libhelix exists. It takes bytes and returns PCM.

```cpp
class MP3Decoder {
public:
    /* 1152 samples per channel is the MP3 frame size, and helix writes
       interleaved, so a stereo frame is 2304 int16_t. */
    static const size_t MAX_SAMPLES = 2304;

    bool begin();                 // allocates helix's working state
    void end();

    /* Decode ONE frame starting at `in`.
     *
     *   >0  int16_t samples written to `pcm`
     *    0  more input needed; nothing consumed beyond any skipped bytes
     *   <0  a frame was dropped and resynced past; carry on
     *
     * `*consumed` is ALWAYS set, in every case, to how many input bytes were
     * taken -- including bytes skipped to reach a sync word. A caller that
     * advances by `*consumed` therefore cannot desynchronise, which is what
     * makes the error path and the mid-stream join the same code. */
    int decodeFrame(const uint8_t *in, size_t inLen, size_t *consumed,
                    int16_t *pcm, size_t pcmCap);

    uint32_t sampleRate() const;  // 0 until the first frame decodes
    uint8_t channels() const;     // 1 or 2
    uint32_t bitrate() const;
    uint32_t frames() const;
    uint32_t errors() const;      // frames dropped and resynced past
};
```

It has no knowledge of networks, rings or sinks, so it is tested against an
MP3 in flash with no I/O at all.

### `IcyStream`

```cpp
class IcyStream : public Stream {
public:
    /* metaint of 0 makes this a pass-through. */
    IcyStream(Stream &upstream, uint32_t metaint);
    const char *title() const;    // "" until a metadata block arrives
    uint32_t titleChanges() const;
    // Stream: available(), read(), peek(), flush(), write()
};
```

This is not decoration. A Shoutcast server interleaves a metadata block every
`icy-metaint` bytes of audio, and leaving those bytes in the stream corrupts
one frame every few seconds — an audible periodic glitch that looks like a
decoder bug. Stripping them is what makes the decoder's error counter mean
something.

### `MP3Player`

```cpp
class MP3Player {
public:
    bool begin(Stream &source, AudioSink &sink);
    bool loop();                  // false once the source is done and drained
    void end();

    bool running() const;
    uint32_t sampleRate() const;  // as decoded
    size_t buffered() const;      // bytes in the ring
    uint32_t underruns() const;   // passed through from the sink
    uint32_t decodeErrors() const;
    uint32_t rateChanges() const;
};
```

`loop()` is non-blocking and does at most one frame of work per call: top the
ring up from the source, and if the sink has room for a frame, decode one and
write it.

Three things live here because neither the decoder nor the sink owns them:

- **The sink starts late.** An MP3 stream's sample rate is not known until the
  first frame decodes, so `begin()` does not start the sink; the first
  successful decode does.
- **Mono is widened.** `AudioSink::writeFrames()` takes interleaved stereo, so
  a mono frame is duplicated into both channels.
- **Playback is primed.** Output does not start until the ring holds at least
  `PRIME_BYTES` (16 KB, half the ring, about one second at 128 kbps) or the
  source ends, whichever comes first. Without this the first second is
  guaranteed to underrun.

### Vendored `libhelix-mp3`

Under `libraries/MP3Audio/src/libhelix-mp3/`, taken from ESP8266Audio, with
its RealNetworks licence file alongside it, following the convention the other
vendored libraries here use.

**This is the one component that is not permissively licensed.** libhelix is
under the RealNetworks RPSL/RCSL, not MIT or CC0, and this repository has no
top-level licence of its own. It was chosen over the public-domain minimp3
knowingly, for its fixed-point arithmetic and its record on ESP8266 and ESP32.
Anyone repackaging this core should know it is here.

## Sizing

| Quantity | Value |
|---|---|
| Stream rate at 128 kbps | 16 KB/s |
| Input ring | 32 KB in DTCM, about 2 seconds |
| DTCM free | roughly 200 KB of 255 KB |
| Largest MP3 frame | ~1940 bytes in, 1152 samples/channel out |
| PCM at 44.1 kHz stereo | 176 KB/s |

**PSRAM is deliberately not in this path.** A live radio cannot use a long
buffer — falling minutes behind is not a feature — and two seconds of jitter
absorption fits in internal RAM with room to spare. The ring is a private
implementation detail of `MP3Player`, so if jitter measurements later say
otherwise, it can move behind the same interface without touching the API.

## Error handling

- **Joining mid-stream is normal, not an error.** A radio connection begins at
  an arbitrary byte, so the first decode is a sync-word search that discards
  everything before it. Same code path as a resync.
- **Decoder errors resync rather than stop.** A data underflow means "need
  more bytes" and is not counted. Anything else advances to the next sync word
  and increments `decodeErrors()`. A corrupt frame should glitch, not end the
  broadcast.
- **A sample-rate change restarts the sink** at the new rate and increments
  `rateChanges()`. Playing on at the wrong speed would be audible and silent
  in the logs, which is the worst combination.
- **A stall or disconnect stops cleanly and reports it.** `MP3Player` does not
  reconnect. That policy belongs to the sketch: a retry loop buried in a
  library is neither testable nor overridable. The example reconnects.
- **Underruns are passed through from `AudioSink::underruns()`**, not counted
  again, because `I2S` already counts them correctly. That one number is what
  says the network or the CPU failed to keep up.
- **Ring overflow is structurally impossible** — the player only ever reads as
  much as the ring has room for.

## Testing

One test MP3, committed at `tests/data/tone.mp3`: two seconds of a 440 Hz
sine at 44.1 kHz, encoded mono at 64 kbps, which is about 16 KB. Generated
once with

```
ffmpeg -f lavfi -i "sine=frequency=440:duration=2:sample_rate=44100"        -ac 1 -b:a 64k tests/data/tone.mp3
```

and committed rather than generated per-run, so the tests do not depend on
ffmpeg being installed or on two ffmpeg versions encoding identically. A tool
regenerates the C array the test sketch embeds, and a test asserts the
embedded copy and the file checksum equal so the two cannot diverge.

**Checksum means FNV-1a 32-bit over the little-endian PCM bytes**, chosen
because it is ten lines on the board and one line in Python, and the point is
detecting divergence rather than resisting an adversary. Decoded PCM must be
compared this way and not sample-by-sample over serial: two seconds of stereo
is 350 KB, which is minutes at 115200 baud.

Everything is silent: a counting `AudioSink` in the test sketch checksums PCM
rather than driving the speaker. There is a real amplifier on I2S1's pins.

1. **Decoder alone**, against the flash MP3: sample rate, channels, frame
   count and a checksum over all decoded PCM. No network, no sink.
2. **`IcyStream`**, against a synthetic stream with a known `metaint`: the
   stripped output equals the payload exactly, and the title parses.
3. **Player**, from a memory `Stream` into the counting sink: frames
   delivered, zero decode errors, zero underruns.
4. **Network path**, reusing `test_webserver.py`'s local server: the board
   fetches the same MP3 over Ethernet and must produce **the same checksum as
   test 1**. That equality is the assertion — it proves the network path
   changes nothing about the audio.
5. **HTTPS variant** of 4, reusing `test_tls_server.py` plus `rtcset`.
6. **Stall behaviour**: the server delivers below real time. Underruns must be
   non-zero *and* playback must continue rather than stop.

Not tested, deliberately: whether it sounds right. The I2S suite already
proves the transmit path clocks data at the rate the divider claims, and the
rest needs ears or a scope.

## Examples

- **`WebRadio`** — Ethernet up, RTC set, HTTPS GET, `IcyStream`, `MP3Player`
  into `I2S`, printing the ICY title as it changes and reconnecting on
  disconnect. **Starts at low volume**, in keeping with every other audio
  example here, because a real speaker is attached and this is the first thing
  anyone runs.
- **`PlayFromSD`** — the same player fed by a `File` instead of a socket, which
  exists to demonstrate that the source is just a `Stream`, and to give
  somewhere to debug decoding without a network.

## Risks

- **ICY responses are not always HTTP.** Some Shoutcast servers answer
  `ICY 200 OK` rather than `HTTP/1.0 200 OK`, which a strict client will fail
  to parse. If `HTTPClient` rejects such a response, the fallback is to issue
  the request over a raw `Client` and parse the status line leniently. This is
  the most likely thing to need a workaround, and it is a station-by-station
  property rather than something the bench can settle.
- **TLS and decode share one core.** A 128 kbps stream is 16 KB/s, so record
  decryption is a small load next to decoding, but the handshake is not, and
  it happens while nothing is buffered yet. Priming the ring before starting
  the sink covers this; the stall test is what proves it.
- **Decode cost is unmeasured on this part.** libhelix sustains 44.1 kHz
  stereo on an ESP8266 at 80 MHz, and the V5F runs at 400 MHz with an
  instruction cache, so there is ample margin on paper. Task 1 measures it
  rather than assuming.
