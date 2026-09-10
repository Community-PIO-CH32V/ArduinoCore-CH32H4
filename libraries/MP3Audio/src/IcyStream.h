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

    IcyStream(Stream &upstream, uint32_t metaint)
        : _up(upstream), _metaint(metaint), _untilMeta(metaint) { }

    /* "" until a metadata block carrying a StreamTitle arrives. */
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
