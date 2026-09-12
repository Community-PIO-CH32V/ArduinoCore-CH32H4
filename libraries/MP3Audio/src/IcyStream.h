/* A Stream filter that removes Shoutcast metadata.
 *
 * A Shoutcast server sends `icy-metaint` bytes of audio, then one length byte
 * L, then 16*L bytes of metadata, and repeats. Leaving those bytes in the
 * audio corrupts one frame every few seconds. The glitch is periodic and
 * sounds like a decoder fault, which is why this is not optional.
 *
 *     HTTPClient http;
 *     const char *keys[] = { "icy-metaint" };
 *     http.collectHeaders(keys, 1);
 *     http.GET();
 *     IcyStream icy(http.getStream(), http.header("icy-metaint").toInt());
 *
 * metaint of 0 makes it a pass-through, so a plain MP3 over HTTP needs no
 * special case at the call site.
 */
#pragma once

#include <Arduino.h>

class IcyStream : public Stream {
public:
    static const size_t MAX_TITLE = 128;

    /* Default-constructible and re-bindable, so a sketch can declare one
       static and point it at each new connection. The metaint is not known
       until the response headers arrive, so a constructor-only design forces
       new/delete per connection -- which on an MCU is worth avoiding, and
       which also trips -Wdelete-non-virtual-dtor through the Stream base. */
    IcyStream() { }
    IcyStream(Stream &upstream, uint32_t metaint) { begin(upstream, metaint); }
    virtual ~IcyStream() { }

    void begin(Stream &upstream, uint32_t metaint) {
        _up = &upstream;
        _metaint = metaint;
        _untilMeta = metaint;
        _titleChanges = 0;
        _title[0] = '\0';
        /* Any half-read block belonged to the previous connection. */
        _metaLeft = 0;
        _metaGot = 0;
    }

    /* "" until a metadata block carrying a StreamTitle arrives. */
    const char *title() const { return _title; }
    uint32_t titleChanges() const { return _titleChanges; }

    /* Audio bytes still to come before the next metadata block. Zero means the
       next byte off the wire is the block's length byte, so this reading zero
       for any length of time says the stream is waiting at a boundary rather
       than being slow. Exposed because that distinction is invisible from the
       outside and was needed to find a starvation bug. */
    uint32_t untilMeta() const { return _untilMeta; }

    /* Bytes the upstream has, ignoring the metadata boundary. Compare with
       available(): upstream non-zero while available() is zero means the
       boundary is what is blocking, not the network. */
    int upstreamAvailable() { return _up ? _up->available() : 0; }

    int available() override;
    int read() override;
    int peek() override { return _up ? _up->peek() : -1; }
    void flush() override { if (_up) { _up->flush(); } }
    size_t write(uint8_t b) override { return _up ? _up->write(b) : 0; }

private:
    void consumeMetadata();

    Stream *_up = nullptr;
    uint32_t _metaint = 0;
    uint32_t _untilMeta = 0;
    uint32_t _titleChanges = 0;

    /* HALF-READ METADATA BLOCK, carried between calls.
     *
     * A block is one length byte plus up to 4080 bytes of text, and TCP is
     * free to split that across segments. Reading it with a non-blocking
     * read() therefore has to be resumable: `_metaLeft` is what is still owed,
     * and the audio counter is NOT rearmed until it reaches zero.
     *
     * Getting this wrong was subtle rather than loud. The rest of the block
     * was skipped, the counter rearmed early, and the leftover text went to
     * the decoder as audio -- a handful of decode errors, a title truncated
     * mid-word, and every later boundary off by however many bytes were
     * missed. */
    uint32_t _metaLeft = 0;
    uint16_t _metaGot = 0;
    char _metaBuf[MAX_TITLE];
    char _title[MAX_TITLE] = {0};
};
