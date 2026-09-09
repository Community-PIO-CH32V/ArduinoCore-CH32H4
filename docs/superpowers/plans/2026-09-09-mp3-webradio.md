# MP3 Web Radio Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Play an MP3 internet radio stream over HTTPS out of the I2S port.

**Architecture:** Four units in one new library. `MP3Decoder` wraps the vendored libhelix and knows nothing else. `IcyStream` is a `Stream` filter that strips Shoutcast metadata. `MP3Player` owns a 32 KB ring, pulls from any `Stream`, and writes to any `AudioSink`. Because the ends are a `Stream` and an `AudioSink`, playing from SD is the same code with a different source and the DACs are a different sink.

**Tech Stack:** libhelix-mp3 (vendored), the existing `HTTPClient`, `EthernetClientSecure`, `I2S` and `AudioSink`, and the pytest hardware harness in `tests/hw`.

**Spec:** `docs/superpowers/specs/2026-09-09-mp3-webradio-design.md`

## Global Constraints

- **New library: `libraries/MP3Audio`.** Version `1.0.0`, `author=Community-PIO-CH32V`, `architectures=ch32h4`.
- **libhelix is RealNetworks RPSL/RCSL licensed.** It goes in `libraries/MP3Audio/src/libhelix-mp3/` **with its licence file intact**, and it is already listed in `THIRD-PARTY.md` under "Planned" — move it to a real section in Task 1. Do not strip or rewrite its headers. `LICENSE` covers only files with no notice of their own, so leaving those headers in place is what keeps the scoping true.
- **Nothing in the test suite may make sound.** There is a real amplifier and speaker on I2S1 (PB12/PB13/PB15). Every test uses a counting `AudioSink` in the test sketch, never `I2S`. Only the `WebRadio` example drives the speaker, and it starts at low volume.
- **Ring: 32 KB. `PRIME_BYTES`: 16 KB.** Output does not start until the ring holds `PRIME_BYTES` or the source ends.
- **Checksum means FNV-1a 32-bit over little-endian PCM bytes** (offset basis `2166136261`, prime `16777619`). Never compare PCM sample-by-sample over serial: two seconds of stereo is 350 KB, which is minutes at 115200 baud.
- **Test MP3:** `tests/data/tone.mp3`, two seconds of 440 Hz at 44.1 kHz, mono, 64 kbps, generated once and committed:
  ```
  ffmpeg -f lavfi -i "sine=frequency=440:duration=2:sample_rate=44100" \
         -ac 1 -b:a 64k tests/data/tone.mp3
  ```
- **`AudioSink` (in `cores/ch32h4/AudioSink.h`) is the output contract**, and every signature below is exact:
  ```cpp
  virtual bool begin(uint32_t sampleRate) = 0;
  virtual bool end() = 0;
  virtual bool running() const = 0;
  virtual uint32_t sampleRate() const = 0;
  virtual size_t writeFrame(int16_t left, int16_t right) = 0;
  virtual size_t writeFrames(const int16_t *interleaved, size_t frames) = 0;
  virtual size_t availableFrames() = 0;
  virtual uint32_t underruns() const = 0;
  ```
- **Hardware tests use the pinned wlink** at `tools/bin/wlink.exe` (0.1.2). PlatformIO's 0.1.1 misreports this part.
- Commit messages end with:
  ```
  Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
  Claude-Session: https://claude.ai/code/session_01MwRqfix3zqKNJy2o4pQwvV
  ```

---

## File Structure

**Created:**

| File | Responsibility |
|---|---|
| `libraries/MP3Audio/library.properties` | metadata |
| `libraries/MP3Audio/src/MP3Decoder.{h,cpp}` | the only code that knows libhelix exists |
| `libraries/MP3Audio/src/IcyStream.{h,cpp}` | strips Shoutcast metadata from a `Stream` |
| `libraries/MP3Audio/src/MP3Player.{h,cpp}` | the ring and the pump |
| `libraries/MP3Audio/src/libhelix-mp3/` | vendored decoder, licence intact |
| `libraries/MP3Audio/examples/WebRadio/WebRadio.ino` | the deliverable |
| `libraries/MP3Audio/examples/PlayFromSD/PlayFromSD.ino` | proves the source is just a `Stream` |
| `tests/data/tone.mp3` | the one test fixture |
| `tests/data/tone_mp3.h` | the same bytes as a C array, generated |
| `tools/mp3_to_header.py` | generates the above; keeps them from diverging |
| `tests/sketches/mp3audio/{platformio.ini,src/main.cpp}` | test sketch, with the counting sink |
| `tests/hw/test_mp3audio.py` | decoder, IcyStream and player tests |
| `tests/hw/test_mp3radio.py` | the network tests |

**Modified:** `tests/hw/conftest.py` (one fixture plus its `BOARD_FIXTURES` entry), `THIRD-PARTY.md`, `docs/hazards.md`.

Each of the three units is one pair of files with one responsibility, and each is testable without the other two.

---

## Task 1: Vendor libhelix and wrap it in `MP3Decoder`

Decodes an MP3 held in flash. No network, no ring, no sink.

**Files:**
- Create: `libraries/MP3Audio/library.properties`, `libraries/MP3Audio/src/MP3Decoder.{h,cpp}`, `libraries/MP3Audio/src/libhelix-mp3/`, `tests/data/tone.mp3`, `tests/data/tone_mp3.h`, `tools/mp3_to_header.py`, `tests/sketches/mp3audio/{platformio.ini,src/main.cpp}`, `tests/hw/test_mp3audio.py`
- Modify: `tests/hw/conftest.py`, `THIRD-PARTY.md`

**Interfaces:**
- Produces: `class MP3Decoder` with `bool begin()`, `void end()`, `int decodeFrame(const uint8_t *in, size_t inLen, size_t *consumed, int16_t *pcm, size_t pcmCap)`, `uint32_t sampleRate() const`, `uint8_t channels() const`, `uint32_t bitrate() const`, `uint32_t frames() const`, `uint32_t errors() const`, and `static const size_t MAX_SAMPLES = 2304`.

- [ ] **Step 1: Vendor the decoder**

Take `src/libhelix-mp3/` from ESP8266Audio (https://github.com/earlephilhower/ESP8266Audio) into `libraries/MP3Audio/src/libhelix-mp3/`. Keep every file header as-is and keep its licence file. Delete only the ESP-specific assembly variants if any are present; the portable C sources are what this builds.

Then move libhelix in `THIRD-PARTY.md` out of the "Planned" table into its own section:

```markdown
## Copyleft-style: RealNetworks RPSL/RCSL

| Component | Where | Origin |
|---|---|---|
| libhelix-mp3 | `libraries/MP3Audio/src/libhelix-mp3/` | RealNetworks, via ESP8266Audio |

Neither MIT nor BSD, and the only component here that is neither permissive
nor LGPL. It was chosen over the public-domain minimp3 for its fixed-point
arithmetic and its record on ESP8266 and ESP32.
```

- [ ] **Step 2: Generate the test fixture**

```bash
mkdir -p tests/data
ffmpeg -f lavfi -i "sine=frequency=440:duration=2:sample_rate=44100" \
       -ac 1 -b:a 64k tests/data/tone.mp3
```

Create `tools/mp3_to_header.py`:

```python
"""Turn an MP3 into a C array so the decoder test needs no filesystem.

The board and the pytest HTTP server must serve byte-identical data, so both
come from tests/data/tone.mp3 and a test asserts the checksums agree.
"""
import pathlib
import sys

def main(src, dst, name):
    data = pathlib.Path(src).read_bytes()
    lines = [
        "/* GENERATED by tools/mp3_to_header.py from %s -- do not edit. */"
        % pathlib.Path(src).name,
        "#pragma once",
        "#include <stdint.h>",
        "",
        "static const uint32_t %s_len = %d;" % (name, len(data)),
        "static const uint8_t %s[] = {" % name,
    ]
    for i in range(0, len(data), 16):
        lines.append("    " + " ".join("0x%02X," % b for b in data[i:i + 16]))
    lines += ["};", ""]
    pathlib.Path(dst).write_text("\n".join(lines))
    print("%s: %d bytes" % (dst, len(data)))

if __name__ == "__main__":
    main(sys.argv[1], sys.argv[2], sys.argv[3])
```

Run it:

```bash
python tools/mp3_to_header.py tests/data/tone.mp3 tests/data/tone_mp3.h tone_mp3
```

- [ ] **Step 3: Write the failing tests**

Create `tests/hw/test_mp3audio.py`:

```python
"""The MP3 decoder, IcyStream and the player, none of which need a network.

Everything here is SILENT. The sketch's sink counts and checksums frames
instead of driving I2S, because there is a real amplifier on I2S1's pins.
"""
import hashlib
import pathlib

import pytest

from conftest import kv

DATA = pathlib.Path(__file__).resolve().parents[1] / "data" / "tone.mp3"


def fnv1a(data):
    h = 2166136261
    for b in data:
        h = ((h ^ b) * 16777619) & 0xFFFFFFFF
    return h


def test_the_embedded_mp3_matches_the_file_on_disk(mp3_board):
    """The board's copy and the pytest server's copy must be the same bytes.

    They are generated from one file by tools/mp3_to_header.py, and this is
    what catches someone regenerating one and not the other -- which would
    make the network test compare two different recordings and still pass.
    """
    r = kv(mp3_board.command("mp3info", timeout=20))
    assert r["mp3_len"] == DATA.stat().st_size, r.raw
    assert r["mp3_fnv"] == fnv1a(DATA.read_bytes()), r.raw


def test_the_decoder_reports_the_stream_format(mp3_board):
    r = kv(mp3_board.command("decode", timeout=30))
    assert r["rate"] == 44100, r.raw
    assert r["channels"] == 1, r.raw
    assert r["errors"] == 0, "a clean MP3 should decode without a resync"


def test_the_decoder_produces_the_expected_frame_count(mp3_board):
    """2 s at 44.1 kHz in 1152-sample frames is 77 frames, and a decoder that
    silently drops or duplicates one is otherwise invisible."""
    r = kv(mp3_board.command("decode", timeout=30))
    assert 74 <= r["frames"] <= 80, r.raw


def test_decoding_is_deterministic(mp3_board):
    """The same bytes must decode to the same PCM every time. This checksum is
    the value every later test compares against, so it has to be stable before
    it is worth anything."""
    first = kv(mp3_board.command("decode", timeout=30))["pcm_fnv"]
    second = kv(mp3_board.command("decode", timeout=30))["pcm_fnv"]
    assert first == second, "decoding the same buffer twice differed"
```

- [ ] **Step 4: Run them and watch them fail**

```bash
WLINK=tools/bin/wlink.exe python -m pytest tests/hw/test_mp3audio.py -q
```

Expected: every test errors on a missing `mp3_board` fixture.

- [ ] **Step 5: Add the board fixture**

In `tests/hw/conftest.py`, next to `psram_board`:

```python
@pytest.fixture(scope="session")
def mp3_board():
    """The MP3 sketch. Silent: its sink counts frames rather than playing
    them, because a real speaker is wired to I2S1."""
    if serial is None:
        pytest.skip("pyserial is not installed")
    return Board("mp3audio")
```

and add `"mp3_board"` to `BOARD_FIXTURES`.

- [ ] **Step 6: Write the decoder header**

Create `libraries/MP3Audio/src/MP3Decoder.h`:

```cpp
/* An MP3 frame decoder, and nothing else.
 *
 * This class knows about libhelix and about bytes. It does not know where the
 * bytes came from or where the samples are going, which is what lets it be
 * tested against an array in flash with no I/O at all.
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
```

- [ ] **Step 7: Implement the decoder**

Create `libraries/MP3Audio/src/MP3Decoder.cpp`:

```cpp
#include "MP3Decoder.h"

extern "C" {
#include "libhelix-mp3/mp3dec.h"
}

bool MP3Decoder::begin() {
    if (_dec) { return true; }
    _dec = MP3InitDecoder();
    _rate = 0; _bitrate = 0; _frames = 0; _errors = 0; _channels = 0;
    return _dec != nullptr;
}

void MP3Decoder::end() {
    if (_dec) {
        MP3FreeDecoder((HMP3Decoder)_dec);
        _dec = nullptr;
    }
}

int MP3Decoder::decodeFrame(const uint8_t *in, size_t inLen, size_t *consumed,
                            int16_t *pcm, size_t pcmCap) {
    *consumed = 0;
    if (!_dec || !in || inLen == 0 || pcmCap < MAX_SAMPLES) { return 0; }

    /* Find the frame. A radio stream begins at an arbitrary byte, so this is
       the normal path and not just the error path. */
    const int off = MP3FindSyncWord((unsigned char *)in, (int)inLen);
    if (off < 0) {
        /* No sync anywhere. Keep the last two bytes: a sync word may straddle
           the end of what we have been given. */
        *consumed = inLen > 2 ? inLen - 2 : 0;
        return 0;
    }
    *consumed = (size_t)off;

    unsigned char *p = (unsigned char *)in + off;
    int left = (int)(inLen - (size_t)off);
    const int before = left;

    const int rc = MP3Decode((HMP3Decoder)_dec, &p, &left, pcm, 0);
    if (rc == ERR_MP3_INDATA_UNDERFLOW || rc == ERR_MP3_MAINDATA_UNDERFLOW) {
        /* Not an error: the frame is real but incomplete. Consume only the
           skipped bytes so the caller retries once more has arrived. */
        return 0;
    }
    if (rc != ERR_MP3_NONE) {
        /* Step past this sync word so the next search cannot find it again
           and loop forever. */
        *consumed = (size_t)off + 1;
        _errors++;
        return -1;
    }

    *consumed = (size_t)off + (size_t)(before - left);

    MP3FrameInfo info;
    MP3GetLastFrameInfo((HMP3Decoder)_dec, &info);
    _rate = (uint32_t)info.samprate;
    _channels = (uint8_t)info.nChans;
    _bitrate = (uint32_t)info.bitrate;
    _frames++;
    return info.outputSamps;
}
```

Create `libraries/MP3Audio/library.properties`:

```
name=MP3Audio
version=1.0.0
author=Community-PIO-CH32V
maintainer=Community-PIO-CH32V
sentence=MP3 decoding and playback into any AudioSink.
paragraph=Decodes MP3 with the Helix fixed-point decoder and plays it into I2S or the DACs through the core's AudioSink interface. The source is an ordinary Stream, so an HTTP socket, a file on SD and an array in flash are all the same code. Includes a web radio example. The vendored Helix decoder is RealNetworks RPSL licensed; see THIRD-PARTY.md.
category=Signal Input/Output
architectures=ch32h4
```

- [ ] **Step 8: Write the test sketch**

Create `tests/sketches/mp3audio/platformio.ini`:

```ini
[env:ch32h417]
platform = symlink://C:/Users/Max/.platformio/platforms/ch32v
board = ch32h417qeu6_evt_r0
framework = arduino
platform_packages =
    framework-arduinoch32h4 @ symlink://C:/Users/Max/temp/arduino-core-ch32h4
build_flags = -I${PROJECT_DIR}/../../data
```

Create `tests/sketches/mp3audio/src/main.cpp`:

```cpp
/* MP3Audio, on Serial1. Nothing here makes a sound.
 *
 * CountingSink is an AudioSink that checksums frames instead of clocking them
 * out of I2S. A real amplifier and speaker are wired to I2S1, and no test
 * needs to hear anything to know whether the decoder works.
 */
#include <Arduino.h>
#include <MP3Decoder.h>
#include "tone_mp3.h"

static char line[96];
static int len = 0;

static uint32_t fnv1a(uint32_t h, const uint8_t *p, size_t n) {
  for (size_t i = 0; i < n; i++) {
    h = (h ^ p[i]) * 16777619u;
  }
  return h;
}

static MP3Decoder decoder;
static int16_t pcm[MP3Decoder::MAX_SAMPLES];

static void handle(const char *cmd) {
  if (!strcmp(cmd, "mp3info")) {
    Serial1.print("mp3_len="); Serial1.println(tone_mp3_len);
    Serial1.print("mp3_fnv=");
    Serial1.println(fnv1a(2166136261u, tone_mp3, tone_mp3_len));

  } else if (!strcmp(cmd, "decode")) {
    decoder.end();
    if (!decoder.begin()) { Serial1.println("began=0"); Serial1.print("> "); return; }
    uint32_t h = 2166136261u;
    size_t off = 0;
    while (off < tone_mp3_len) {
      size_t used = 0;
      const int n = decoder.decodeFrame(tone_mp3 + off, tone_mp3_len - off,
                                        &used, pcm, MP3Decoder::MAX_SAMPLES);
      if (used == 0 && n <= 0) { break; }   /* out of data */
      off += used;
      if (n > 0) { h = fnv1a(h, (const uint8_t *)pcm, (size_t)n * 2); }
    }
    Serial1.print("rate="); Serial1.println(decoder.sampleRate());
    Serial1.print("channels="); Serial1.println(decoder.channels());
    Serial1.print("bitrate="); Serial1.println(decoder.bitrate());
    Serial1.print("frames="); Serial1.println(decoder.frames());
    Serial1.print("errors="); Serial1.println(decoder.errors());
    Serial1.print("pcm_fnv="); Serial1.println(h);

  } else {
    Serial1.print("unknown: "); Serial1.println(cmd);
  }
  Serial1.print("> ");
}

void setup() {
  Serial1.begin(115200);
  Serial1.println();
  Serial1.println("mp3audio test");
  Serial1.print("> ");
}

void loop() {
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (len) { line[len] = '\0'; handle(line); len = 0; }
    } else if (len < (int)sizeof(line) - 1) {
      line[len++] = c;
    }
  }
  yield();
}
```

- [ ] **Step 9: Run the tests until they pass**

```bash
cd tests/sketches/mp3audio && pio run && cd -
WLINK=tools/bin/wlink.exe python -m pytest tests/hw/test_mp3audio.py -q -s
```

Expected: 4 passed. **Record the `pcm_fnv` value** printed by `decode` — Task 3's network test asserts equality against this same number, produced by the same board, so it does not need to be hard-coded anywhere.

If `frames` is outside 74–80, do not widen the bound without understanding why: a decoder dropping frames is exactly what that assertion is for.

- [ ] **Step 10: Measure the decode cost**

The spec records decode cost as unmeasured on this part. Add a temporary line to the `decode` command that times the loop with `micros()` and prints `decode_us`, run it once, and work out the real-time margin: 2 seconds of audio decoded in `decode_us` microseconds gives a margin of `2000000 / decode_us`. Record the number in the commit message and delete the temporary line.

A margin below about 3x is a problem worth raising before Task 3, because TLS and lwIP have to run in the gaps.

- [ ] **Step 11: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
MP3Audio: vendor libhelix and wrap it in MP3Decoder

The decoder knows about libhelix and about bytes, and nothing else -- not
where the bytes came from, not where the samples go. That is what lets it be
tested against an array in flash with no network, no ring and no sink.

decodeFrame() always sets *consumed, including on the error and
need-more-input paths, so a caller that advances by it cannot desynchronise.
That single rule makes joining a radio stream mid-frame and recovering from a
corrupt frame the same code path, which is the case a decoder wrapper usually
gets wrong.

The test fixture is one MP3 generated once by ffmpeg and committed, embedded
in the sketch by tools/mp3_to_header.py and served from the same file by the
network tests in task 3. A test asserts the two copies checksum equal, because
regenerating one and not the other would leave the network test comparing two
different recordings and still passing.

libhelix is RealNetworks RPSL licensed, moved in THIRD-PARTY.md from Planned
to a section of its own. Its headers are untouched, which is what keeps
LICENSE's "files with no notice of their own" scoping true.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01MwRqfix3zqKNJy2o4pQwvV
EOF
)"
```

---

## Task 2: `IcyStream` and `MP3Player`

Plays from a `Stream` into an `AudioSink`, with no network involved.

**Files:**
- Create: `libraries/MP3Audio/src/IcyStream.{h,cpp}`, `libraries/MP3Audio/src/MP3Player.{h,cpp}`
- Modify: `tests/sketches/mp3audio/src/main.cpp`, `tests/hw/test_mp3audio.py`

**Interfaces:**
- Consumes: `MP3Decoder` from Task 1, exactly as declared there.
- Produces: `class IcyStream : public Stream` with `IcyStream(Stream &upstream, uint32_t metaint)`, `const char *title() const`, `uint32_t titleChanges() const`. `class MP3Player` with `bool begin(Stream &source, AudioSink &sink)`, `bool loop()`, `void end()`, `bool running() const`, `uint32_t sampleRate() const`, `size_t buffered() const`, `uint32_t underruns() const`, `uint32_t decodeErrors() const`, `uint32_t rateChanges() const`.

- [ ] **Step 1: Write the failing tests**

Append to `tests/hw/test_mp3audio.py`:

```python
def test_icystream_strips_metadata_and_reads_the_title(mp3_board):
    """Shoutcast interleaves a metadata block every icy-metaint bytes. Leaving
    those bytes in corrupts one frame every few seconds -- an audible periodic
    glitch that looks exactly like a decoder bug."""
    r = kv(mp3_board.command("icytest", timeout=20))
    assert r["payload_ok"] == 1, "the stripped bytes did not match the payload"
    assert r["title"] == "Artist - Track", r.raw
    assert r["title_changes"] == 1, r.raw


def test_icystream_with_no_metaint_is_a_passthrough(mp3_board):
    """A stream without icy-metaint must not be altered at all."""
    r = kv(mp3_board.command("icypass", timeout=20))
    assert r["payload_ok"] == 1, r.raw
    assert r["title_changes"] == 0, r.raw


def test_the_player_delivers_every_frame_to_the_sink(mp3_board):
    """Player from a memory Stream into the counting sink. Same MP3 and same
    checksum as the decoder test, so a player that loses or reorders a frame
    shows up here rather than as a glitch someone hears later."""
    r = kv(mp3_board.command("play", timeout=40))
    assert r["decode_errors"] == 0, r.raw
    assert r["underruns"] == 0, "the counting sink cannot underrun"
    assert r["rate"] == 44100, r.raw
    direct = kv(mp3_board.command("decode", timeout=30))["pcm_fnv"]
    assert r["pcm_fnv"] == direct, (
        "the player's PCM differs from the decoder's on the same input")


def test_the_player_widens_mono_to_stereo(mp3_board):
    """AudioSink takes interleaved stereo. The test MP3 is mono, so every
    frame must arrive as two identical channels rather than at half length or
    half speed."""
    r = kv(mp3_board.command("play", timeout=40))
    assert r["sink_frames"] > 0, r.raw
    assert r["mono_widened"] == 1, "left and right differed on a mono source"
```

- [ ] **Step 2: Run them and watch them fail**

```bash
WLINK=tools/bin/wlink.exe python -m pytest tests/hw/test_mp3audio.py -q -k "icy or player or widens"
```

Expected: unknown-command replies, so `payload_ok` and friends are missing.

- [ ] **Step 3: Write `IcyStream`**

Create `libraries/MP3Audio/src/IcyStream.h`:

```cpp
/* A Stream filter that removes Shoutcast metadata.
 *
 * A Shoutcast server sends `icy-metaint` bytes of audio, then one length byte
 * L, then 16*L bytes of metadata, and repeats. Leaving those bytes in the
 * audio corrupts one frame every few seconds. The glitch is periodic and
 * sounds like a decoder fault, which is why this is not optional.
 */
#pragma once

#include <Arduino.h>

class IcyStream : public Stream {
public:
    static const size_t MAX_TITLE = 128;

    /* metaint of 0 makes this a pass-through. */
    IcyStream(Stream &upstream, uint32_t metaint)
        : _up(upstream), _metaint(metaint), _untilMeta(metaint) { }

    const char *title() const { return _title; }
    uint32_t titleChanges() const { return _titleChanges; }

    int available() override;
    int read() override;
    int peek() override { return _up.peek(); }
    void flush() override { _up.flush(); }
    size_t write(uint8_t b) override { return _up.write(b); }

private:
    void consumeMetadata();

    Stream &_up;
    uint32_t _metaint;
    uint32_t _untilMeta;
    uint32_t _titleChanges = 0;
    char _title[MAX_TITLE] = {0};
};
```

Create `libraries/MP3Audio/src/IcyStream.cpp`:

```cpp
#include "IcyStream.h"

int IcyStream::available() {
    if (_metaint == 0) { return _up.available(); }
    if (_untilMeta == 0) { consumeMetadata(); }
    const int a = _up.available();
    return a < (int)_untilMeta ? a : (int)_untilMeta;
}

int IcyStream::read() {
    if (_metaint == 0) { return _up.read(); }
    if (_untilMeta == 0) { consumeMetadata(); }
    const int c = _up.read();
    if (c >= 0 && _untilMeta > 0) { _untilMeta--; }
    return c;
}

/* Called with the audio counter at zero. Reads the length byte and the block,
 * then rearms. Blocks only on bytes the server has already promised. */
void IcyStream::consumeMetadata() {
    const int lenByte = _up.read();
    if (lenByte < 0) { return; }             /* nothing there yet; retry later */
    _untilMeta = _metaint;
    size_t n = (size_t)lenByte * 16u;
    if (n == 0) { return; }                  /* the common case: no change */

    char buf[MAX_TITLE];
    size_t got = 0;
    while (n--) {
        const int c = _up.read();
        if (c < 0) { break; }
        if (got + 1 < sizeof(buf)) { buf[got++] = (char)c; }
    }
    buf[got] = '\0';

    /* StreamTitle='...'; is the only field anyone cares about. */
    const char *s = strstr(buf, "StreamTitle='");
    if (!s) { return; }
    s += 13;
    const char *e = strstr(s, "';");
    if (!e) { e = s + strlen(s); }
    size_t tl = (size_t)(e - s);
    if (tl >= MAX_TITLE) { tl = MAX_TITLE - 1; }
    if (tl == strlen(_title) && strncmp(_title, s, tl) == 0) { return; }
    memcpy(_title, s, tl);
    _title[tl] = '\0';
    _titleChanges++;
}
```

- [ ] **Step 4: Write `MP3Player`**

Create `libraries/MP3Audio/src/MP3Player.h`:

```cpp
/* Pulls MP3 from a Stream, decodes it, and writes frames to an AudioSink.
 *
 * The ends are deliberately interfaces: an HTTP socket, a File on SD and an
 * array in flash are all Streams, and I2S and DACAudio are both AudioSinks.
 * Playing from a different place is a different argument, not different code.
 *
 * loop() is non-blocking and does at most one frame of work per call.
 */
#pragma once

#include <Arduino.h>
#include <AudioSink.h>

#include "MP3Decoder.h"

class MP3Player {
public:
    static const size_t RING_BYTES = 32u * 1024u;
    /* Output waits for this much before starting. Without it the first second
       underruns every time. */
    static const size_t PRIME_BYTES = 16u * 1024u;

    bool begin(Stream &source, AudioSink &sink);
    void end();

    /* False once the source is done and the ring is drained. */
    bool loop();

    bool running() const { return _running; }
    uint32_t sampleRate() const { return _decoder.sampleRate(); }
    size_t buffered() const { return _fill; }
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
```

Create `libraries/MP3Audio/src/MP3Player.cpp`:

```cpp
#include "MP3Player.h"

bool MP3Player::begin(Stream &source, AudioSink &sink) {
    if (_running) { return false; }
    if (!_decoder.begin()) { return false; }
    _src = &source;
    _sink = &sink;
    _fill = 0;
    _rateChanges = 0;
    _startedRate = 0;
    _primed = false;
    _running = true;
    return true;
}

void MP3Player::end() {
    if (_sink && _sink->running()) { _sink->end(); }
    _decoder.end();
    _running = false;
    _src = nullptr;
    _sink = nullptr;
}

/* Top the ring up. The ring is a plain front-aligned buffer rather than a
   circular one: the decoder needs a contiguous run to find a sync word in, and
   memmove of at most 32 KB is cheap next to decoding a frame. */
size_t MP3Player::fillRing() {
    size_t room = RING_BYTES - _fill;
    size_t got = 0;
    while (room) {
        const int avail = _src->available();
        if (avail <= 0) { break; }
        size_t want = (size_t)avail < room ? (size_t)avail : room;
        const int n = _src->readBytes(_ring + _fill, want);
        if (n <= 0) { break; }
        _fill += (size_t)n;
        room -= (size_t)n;
        got += (size_t)n;
    }
    return got;
}

bool MP3Player::decodeOne() {
    size_t used = 0;
    const int samples = _decoder.decodeFrame(_ring, _fill, &used,
                                             _pcm, MP3Decoder::MAX_SAMPLES);
    if (used) {
        memmove(_ring, _ring + used, _fill - used);
        _fill -= used;
    }
    if (samples <= 0) { return false; }

    /* The rate is not known until a frame decodes, so the sink starts here
       rather than in begin(). A mid-stream rate change restarts it. */
    const uint32_t rate = _decoder.sampleRate();
    if (!_sink->running()) {
        if (!_sink->begin(rate)) { return false; }
        _startedRate = rate;
    } else if (rate != _startedRate) {
        _sink->end();
        if (!_sink->begin(rate)) { return false; }
        _startedRate = rate;
        _rateChanges++;
    }

    /* AudioSink takes interleaved stereo. Mono is duplicated rather than
       played at half length, which would come out at double speed. */
    const size_t frames = (_decoder.channels() == 2)
                        ? (size_t)samples / 2u : (size_t)samples;
    const int16_t *out = _pcm;
    if (_decoder.channels() == 1) {
        for (size_t i = 0; i < frames; i++) {
            _stereo[i * 2] = _pcm[i];
            _stereo[i * 2 + 1] = _pcm[i];
        }
        out = _stereo;
    }

    size_t done = 0;
    while (done < frames) {
        const size_t n = _sink->writeFrames(out + done * 2, frames - done);
        if (n == 0) { break; }        /* full; the rest waits for the next loop */
        done += n;
    }
    return true;
}

bool MP3Player::loop() {
    if (!_running) { return false; }

    fillRing();

    if (!_primed) {
        const bool sourceDone = _src->available() <= 0;
        if (_fill >= PRIME_BYTES || (sourceDone && _fill > 0)) {
            _primed = true;
        } else {
            return true;              /* still filling; not an error */
        }
    }

    if (_sink->running() && _sink->availableFrames() < 1152) {
        return true;                  /* no room for a frame yet */
    }

    if (_fill == 0) {
        _running = _src->available() > 0;
        return _running;
    }
    decodeOne();
    return true;
}
```

- [ ] **Step 5: Add the counting sink and the sketch commands**

In `tests/sketches/mp3audio/src/main.cpp`, add above `handle()`:

```cpp
#include <AudioSink.h>
#include <IcyStream.h>
#include <MP3Player.h>

/* An AudioSink that counts and checksums instead of making noise. Its
   availableFrames() is deliberately huge so the player is never throttled and
   underruns() is always zero -- this measures the player, not the hardware. */
class CountingSink : public AudioSink {
public:
    bool begin(uint32_t rate) override { _rate = rate; _running = true; return true; }
    bool end() override { _running = false; return true; }
    bool running() const override { return _running; }
    uint32_t sampleRate() const override { return _rate; }
    size_t writeFrame(int16_t l, int16_t r) override {
        if (l != r) { _monoBroken = true; }
        _fnv = fnv1a(_fnv, (const uint8_t *)&l, 2);
        _frames++;
        return 1;
    }
    size_t writeFrames(const int16_t *pcm, size_t frames) override {
        for (size_t i = 0; i < frames; i++) {
            if (pcm[i * 2] != pcm[i * 2 + 1]) { _monoBroken = true; }
            _fnv = fnv1a(_fnv, (const uint8_t *)&pcm[i * 2], 2);
        }
        _frames += frames;
        return frames;
    }
    size_t availableFrames() override { return 65535; }
    uint32_t underruns() const override { return 0; }

    void reset() { _fnv = 2166136261u; _frames = 0; _monoBroken = false; }
    uint32_t fnv() const { return _fnv; }
    uint32_t frames() const { return _frames; }
    bool monoOk() const { return !_monoBroken; }

private:
    uint32_t _rate = 0, _fnv = 2166136261u, _frames = 0;
    bool _running = false, _monoBroken = false;
};

/* A Stream over an array in flash, so the player can be driven with no
   network and no filesystem. */
class MemStream : public Stream {
public:
    MemStream(const uint8_t *p, size_t n) : _p(p), _n(n) { }
    int available() override { return (int)(_n - _i); }
    int read() override { return _i < _n ? _p[_i++] : -1; }
    int peek() override { return _i < _n ? _p[_i] : -1; }
    void flush() override { }
    size_t write(uint8_t) override { return 0; }
private:
    const uint8_t *_p; size_t _n; size_t _i = 0;
};

static CountingSink sink;
static MP3Player player;
```

Add these commands before the unknown-command `else`:

```cpp
  } else if (!strcmp(cmd, "play")) {
    static MemStream src(tone_mp3, tone_mp3_len);
    src = MemStream(tone_mp3, tone_mp3_len);
    sink.reset();
    player.end();
    player.begin(src, sink);
    uint32_t guard = 0;
    while (player.loop() && ++guard < 200000u) { }
    Serial1.print("rate="); Serial1.println(player.sampleRate());
    Serial1.print("sink_frames="); Serial1.println(sink.frames());
    Serial1.print("pcm_fnv="); Serial1.println(sink.fnv());
    Serial1.print("decode_errors="); Serial1.println(player.decodeErrors());
    Serial1.print("underruns="); Serial1.println(player.underruns());
    Serial1.print("rate_changes="); Serial1.println(player.rateChanges());
    Serial1.print("mono_widened="); Serial1.println(sink.monoOk() ? 1 : 0);
    player.end();

  } else if (!strcmp(cmd, "icytest")) {
    /* 32 bytes of payload, a metadata block, then 32 more. */
    static uint8_t raw[128];
    const char *meta = "StreamTitle='Artist - Track';";
    size_t n = 0;
    for (int i = 0; i < 32; i++) { raw[n++] = (uint8_t)i; }
    const size_t blocks = (strlen(meta) + 15) / 16;
    raw[n++] = (uint8_t)blocks;
    for (size_t i = 0; i < blocks * 16; i++) {
      raw[n++] = i < strlen(meta) ? (uint8_t)meta[i] : 0;
    }
    for (int i = 0; i < 32; i++) { raw[n++] = (uint8_t)(32 + i); }
    MemStream up(raw, n);
    IcyStream icy(up, 32);
    int ok = 1;
    for (int i = 0; i < 64; i++) {
      const int c = icy.read();
      if (c != i) { ok = 0; }
    }
    Serial1.print("payload_ok="); Serial1.println(ok);
    Serial1.print("title="); Serial1.println(icy.title());
    Serial1.print("title_changes="); Serial1.println(icy.titleChanges());

  } else if (!strcmp(cmd, "icypass")) {
    static uint8_t raw[64];
    for (int i = 0; i < 64; i++) { raw[i] = (uint8_t)i; }
    MemStream up(raw, 64);
    IcyStream icy(up, 0);
    int ok = 1;
    for (int i = 0; i < 64; i++) { if (icy.read() != i) { ok = 0; } }
    Serial1.print("payload_ok="); Serial1.println(ok);
    Serial1.print("title_changes="); Serial1.println(icy.titleChanges());
```

- [ ] **Step 6: Run the tests until they pass**

```bash
cd tests/sketches/mp3audio && pio run && cd -
WLINK=tools/bin/wlink.exe python -m pytest tests/hw/test_mp3audio.py -q -s
```

Expected: 8 passed. The equality between `play`'s `pcm_fnv` and `decode`'s is the assertion that matters — it says the player changed nothing about the audio.

- [ ] **Step 7: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
MP3Audio: IcyStream and MP3Player

The player takes a Stream and an AudioSink, so an HTTP socket, a File on SD
and an array in flash are the same code with a different argument, and I2S and
the DACs are a different sink. The test drives it from flash, which is why
none of this needs a network yet.

Three things live in the player because neither the decoder nor the sink owns
them. The sample rate is not known until the first frame decodes, so the sink
starts on that frame rather than in begin(), and a mid-stream rate change
restarts it. Mono is widened to interleaved stereo rather than written at half
length, which would play at double speed. And output waits for 16 KB before
starting, without which the first second underruns every time.

IcyStream is not decoration: a Shoutcast server interleaves a metadata block
every icy-metaint bytes, and leaving those bytes in corrupts one frame every
few seconds -- a periodic glitch that sounds exactly like a decoder fault.

The assertion that matters is that the player's PCM checksum equals the
decoder's on the same input, so a player that loses or reorders a frame fails
here rather than as something someone hears later.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01MwRqfix3zqKNJy2o4pQwvV
EOF
)"
```

---

## Task 3: Play it off the network

The same MP3, over Ethernet, decoding to the same checksum.

**Files:**
- Create: `tests/hw/test_mp3radio.py`
- Modify: `tests/sketches/mp3audio/src/main.cpp`, `docs/hazards.md`

**Interfaces:**
- Consumes: `MP3Player`, `IcyStream`, `CountingSink` from Task 2.
- Produces: sketch commands `netplay <url>` and `netplays <url>`.

- [ ] **Step 1: Write the failing tests**

Create `tests/hw/test_mp3radio.py`:

```python
"""The same MP3 over the network, decoding to the same bytes.

The whole file turns on one equality: the board fetching tone.mp3 over HTTP
must produce the PCM checksum it produces decoding tone.mp3 out of flash. That
proves the network path changes nothing about the audio, and it needs no
internet -- the server is this machine.
"""
import http.server
import pathlib
import socket
import threading
import time

import pytest

from conftest import kv

DATA = pathlib.Path(__file__).resolve().parents[1] / "data" / "tone.mp3"
MP3 = DATA.read_bytes()


class _Handler(http.server.BaseHTTPRequestHandler):
    slow = False

    def do_GET(self):
        self.send_response(200)
        self.send_header("Content-Type", "audio/mpeg")
        self.send_header("Content-Length", str(len(MP3)))
        self.end_headers()
        if not self.slow:
            self.wfile.write(MP3)
            return
        # Below real time: 2 s of audio dribbled out over about 6 s.
        step = max(1, len(MP3) // 60)
        for i in range(0, len(MP3), step):
            self.wfile.write(MP3[i:i + step])
            self.wfile.flush()
            time.sleep(0.1)

    def log_message(self, *args):
        pass


@pytest.fixture(scope="module")
def radio(mp3_board):
    ip = kv(mp3_board.banner).get("net_ip", "")
    if not ip or ip == "0.0.0.0":
        pytest.skip("the board has no DHCP lease -- is the RJ45 plugged in?")
    probe = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
    probe.connect((ip, 1234))
    host_ip = probe.getsockname()[0]
    probe.close()
    srv = http.server.ThreadingHTTPServer((host_ip, 0), _Handler)
    threading.Thread(target=srv.serve_forever, daemon=True).start()
    base = "http://%s:%d" % (host_ip, srv.server_address[1])
    yield mp3_board, base
    srv.shutdown()
    srv.server_close()


def test_the_network_path_decodes_to_the_same_bytes(radio):
    """THE test. Anything that corrupts a byte in transit, drops a chunk, or
    mishandles a partial read shows up as a different checksum."""
    board, base = radio
    _Handler.slow = False
    local = kv(board.command("decode", timeout=30))["pcm_fnv"]
    r = kv(board.command("netplay " + base + "/tone.mp3", timeout=60))
    assert r["http_rc"] == 200, r.raw
    assert r["decode_errors"] == 0, r.raw
    assert r["pcm_fnv"] == local, (
        "the stream decoded differently from the same file in flash")


def test_a_slow_server_underruns_but_keeps_playing(radio):
    """A stall must degrade, not stop. Delivered below real time, the sink
    should report underruns AND the stream should still finish with every
    frame intact -- silence is recoverable, a stopped radio is not."""
    board, base = radio
    _Handler.slow = True
    try:
        local = kv(board.command("decode", timeout=30))["pcm_fnv"]
        r = kv(board.command("netplay " + base + "/tone.mp3", timeout=90))
        assert r["http_rc"] == 200, r.raw
        assert r["pcm_fnv"] == local, "a slow stream lost or altered data"
        assert r["decode_errors"] == 0, r.raw
    finally:
        _Handler.slow = False
```

- [ ] **Step 2: Run them and watch them fail**

```bash
WLINK=tools/bin/wlink.exe python -m pytest tests/hw/test_mp3radio.py -q
```

Expected: skipped if the RJ45 is unplugged, otherwise failures on the unknown `netplay` command.

- [ ] **Step 3: Bring the network up in the sketch**

In `tests/sketches/mp3audio/src/main.cpp`, add the includes and a banner line that the fixture reads:

```cpp
#include <lwIP_Ethernet.h>
#include <HTTPClient.h>
```

In `setup()`, after the banner:

```cpp
  Ethernet.begin();
  const uint32_t t0 = millis();
  while (!Ethernet.connected() && millis() - t0 < 15000) { delay(100); }
  Serial1.print("net_ip="); Serial1.println(Ethernet.localIP());
```

Consult `tests/sketches/webserver/src/main.cpp` for the exact bring-up this
core uses and copy it rather than inventing one; the fixture only needs
`net_ip=` in the banner.

- [ ] **Step 4: Add the `netplay` command**

```cpp
  } else if (!strncmp(cmd, "netplay ", 8)) {
    static HTTPClient http;
    static EthernetClient plain;
    http.begin(plain, String(cmd + 8));
    /* icy-metaint has to be collected BEFORE GET(), because HTTPClient only
       keeps headers it was told to keep. */
    static const char *keys[] = { "icy-metaint" };
    http.collectHeaders(keys, 1);
    const int rc = http.GET();
    Serial1.print("http_rc="); Serial1.println(rc);
    if (rc != 200) { http.end(); Serial1.print("> "); return; }

    const uint32_t metaint = (uint32_t)http.header("icy-metaint").toInt();
    IcyStream icy(http.getStream(), metaint);
    sink.reset();
    player.end();
    player.begin(icy, sink);
    const uint32_t start = millis();
    while (player.loop() && millis() - start < 60000u) { }
    Serial1.print("rate="); Serial1.println(player.sampleRate());
    Serial1.print("sink_frames="); Serial1.println(sink.frames());
    Serial1.print("pcm_fnv="); Serial1.println(sink.fnv());
    Serial1.print("decode_errors="); Serial1.println(player.decodeErrors());
    Serial1.print("metaint="); Serial1.println(metaint);
    player.end();
    http.end();
```

- [ ] **Step 5: Run the tests until they pass**

```bash
cd tests/sketches/mp3audio && pio run && cd -
WLINK=tools/bin/wlink.exe python -m pytest tests/hw/test_mp3radio.py -q -s
```

Expected: 2 passed.

If the checksums differ, the fault is almost certainly a partial read: `Stream::available()` on a socket returns what has arrived, not what was asked for, and a `readBytes` that returns short is normal rather than an error. `MP3Player::fillRing()` already handles this; check the sketch is not doing its own reading.

- [ ] **Step 6: Add the HTTPS variant**

Add a `netplays` command identical to `netplay` but using `EthernetClientSecure` with `setInsecure()`, and a test in `tests/hw/test_mp3radio.py` that serves the same file over TLS using `test_tls_server.py`'s certificate helper and calls `rtcset` first:

```python
def test_https_decodes_to_the_same_bytes(radio):
    """TLS must not change the audio either. rtcset comes first because a
    client with an unset clock rejects every certificate -- see hazards.md."""
    board, _base = radio
    board.command("rtcset %d" % int(time.time()), timeout=10)
    local = kv(board.command("decode", timeout=30))["pcm_fnv"]
    r = kv(board.command("netplays " + tls_base + "/tone.mp3", timeout=90))
    assert r["http_rc"] == 200, r.raw
    assert r["pcm_fnv"] == local, "the TLS path altered the audio"
```

Build `tls_base` with a `ThreadingHTTPServer` wrapped in `ssl.SSLContext`, reusing the self-signed certificate generation already in `tests/hw/test_tls_server.py`. Add the `rtcset` command to the sketch, copying it from `tests/sketches/tlstest/src/main.cpp`.

- [ ] **Step 7: Write up what was learned**

Append to `docs/hazards.md` whatever the network work turned up. At minimum, if `HTTPClient` refused an `ICY 200 OK` status line, record that and the workaround; the spec names this as the most likely thing to need one. If it did not come up, record that instead — "the stations tested answered HTTP/1.0" is useful to the next person.

- [ ] **Step 8: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
MP3Audio: play from the network, decoding to the same bytes

The test that matters is an equality: the board fetching tone.mp3 over HTTP
must produce the same PCM checksum it produces decoding tone.mp3 out of flash.
That proves the network path changes nothing about the audio, and it needs no
internet -- the server is the test host.

A second test delivers the same file below real time. A stall has to degrade
rather than stop: every frame must still arrive intact. Silence is
recoverable; a radio that gives up is not.

icy-metaint is collected before GET(), because HTTPClient only keeps headers
it was told to keep, and a metaint that silently reads as zero turns metadata
into corrupt audio.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01MwRqfix3zqKNJy2o4pQwvV
EOF
)"
```

---

## Task 4: The examples, and the regression

**Files:**
- Create: `libraries/MP3Audio/examples/WebRadio/WebRadio.ino`, `libraries/MP3Audio/examples/PlayFromSD/PlayFromSD.ino`

- [ ] **Step 1: Write the WebRadio example**

`libraries/MP3Audio/examples/WebRadio/WebRadio.ino`. It must:

- bring Ethernet up and print the IP;
- set the RTC from a compile-time constant with a comment saying **why**: a TLS client with an unset clock rejects every certificate, and this is the single most likely reason the example fails for someone;
- `GET` the stream over `EthernetClientSecure`, collecting `icy-metaint` before the request;
- wrap the response in `IcyStream`, and print the title whenever `titleChanges()` increases;
- play into `I2S` configured for PB12/PB13/PB15, **with a `VOLUME` constant set to a tenth of full scale and a comment saying it is deliberate**, matching `DACAudio/examples/PlayTone`. A real speaker is attached and this is the first thing anyone runs;
- reconnect on disconnect, with the retry loop in the sketch rather than the library, and say in a comment that this is why the library does not do it;
- print `underruns()` every few seconds, because that is the number that says whether the link is keeping up.

Volume is applied by scaling samples before the sink; add a `setVolume(float)` to `MP3Player` if that is cleaner, and if you do, add a test asserting a scaled frame comes out scaled.

- [ ] **Step 2: Write the PlayFromSD example**

`libraries/MP3Audio/examples/PlayFromSD/PlayFromSD.ino`: open `/track.mp3` from SD, hand the `File` to the same `MP3Player`, play into `I2S` at the same low default volume. The head comment's job is to make the point that this is the *same player* as the radio with a different `Stream`, and to give somewhere to debug decoding with no network in the way.

The SD wiring on this bench is 1-bit: MISO PC8, CLK PC12, MOSI PD2, CS PC11.

- [ ] **Step 3: Build both examples both ways**

```bash
python tools/buildexamples.py MP3Audio
python tools/buildexamples.py --ide MP3Audio
```

- [ ] **Step 4: Full regression**

Run these **strictly one at a time**. Running a build sweep concurrently with anything else exhausted memory on this machine and the job was killed.

```bash
WLINK=tools/bin/wlink.exe python -m pytest tests/hw -q
python -m pytest tests/test_tree.py tests/test_platform_txt.py tests/test_linker.py tests/test_index.py -q
python -m pytest tests/test_build.py -q
python -m pytest tests/test_link_matrix.py -q
python -m pytest tests/test_exceptions_build.py -q
python tools/buildexamples.py
python tools/buildexamples.py --ide
```

- [ ] **Step 5: Commit**

```bash
git add -A
git commit -m "$(cat <<'EOF'
MP3Audio examples: web radio, and the same player from SD

WebRadio is the deliverable. It starts at a tenth of full scale, deliberately
and with a comment saying so, because a real amplifier and speaker are wired
to I2S1 and this is the first thing anyone runs.

It sets the RTC before connecting, with a comment explaining that a TLS client
with an unset clock rejects every certificate -- the single most likely reason
this example fails for someone new. It also reconnects in the sketch rather
than in the library, and says why: a retry policy buried in a library is
neither testable nor overridable.

PlayFromSD exists to make one point: it is the same MP3Player with a File
instead of a socket. It is also where to debug decoding with no network in the
way.

Co-Authored-By: Claude Opus 5 <noreply@anthropic.com>
Claude-Session: https://claude.ai/code/session_01MwRqfix3zqKNJy2o4pQwvV
EOF
)"
```

---

## Self-review notes

**Spec coverage.** `MP3Decoder` → Task 1. `IcyStream` and `MP3Player`, including the three responsibilities the spec assigns to the player (late sink start, mono widening, priming) → Task 2. Network and HTTPS → Task 3. Both examples → Task 4. All six spec tests map: decoder-alone and the embedded/file checksum equality are Task 1, `IcyStream` and player-from-memory are Task 2, the network and HTTPS equalities and the stall test are Task 3. The spec's "decode cost is unmeasured" risk is Task 1 Step 10 rather than left as prose. libhelix's licence is handled in Task 1 Step 1 and reflected in `THIRD-PARTY.md`.

**Type consistency.** `MP3Decoder`'s members are declared in Task 1 Step 6 and used unchanged in Task 2's `MP3Player.cpp`: `MAX_SAMPLES`, `decodeFrame`, `sampleRate()`, `channels()`, `errors()`. `AudioSink`'s eight methods are copied verbatim from the header into the Global Constraints and implemented exactly by `CountingSink`. `MP3Player`'s accessors are declared in Task 2 Step 4 and read by the sketch commands in Task 2 Step 5 and Task 3 Step 4 under the same names.

**Three risks the implementer should carry:**

- **`MP3Player::_ring` is 32 KB and `_stereo` is another 9 KB, both members.** A `MP3Player` on the stack will overflow it. The test sketch and both examples declare it `static`, and anything else should too. This is the most likely cause of a hard fault that looks unrelated.
- **The whole test suite depends on one checksum being stable.** Task 1 Step 9 asserts determinism before anything compares against it. If that test is flaky, stop — every later equality is meaningless, and the fault is in the decoder wrapper, not the network.
- **`ICY 200 OK` is not `HTTP/1.0 200 OK`.** `HTTPClient` may refuse a real station even though every bench test passes, because the bench server is `http.server` and answers properly. Task 3 Step 7 exists to record what actually happened; if it bites, the fallback is a raw `Client` with a lenient status-line parse, and that belongs in the `WebRadio` example rather than in `MP3Player`.
