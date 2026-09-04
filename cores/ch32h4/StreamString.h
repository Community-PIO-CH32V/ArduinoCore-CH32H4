/* A String that is also a Stream.
 *
 * HTTPClient uses one to collect a response body: it writes the body into a
 * Print and then hands the caller a String. That is the whole requirement,
 * and this is deliberately the whole implementation.
 *
 * The esp8266 and arduino-pico cores ship a much larger StreamString built on
 * an S2Stream helper and their own extended Stream, with peek pointers and
 * transfer helpers that only their StreamDev code uses. None of that is
 * reachable through the standard Stream interface this core has, so vendoring
 * it would mean vendoring their Stream as well. A sketch that only ever does
 * what the name promises -- write into it, read out of it, use it as a String
 * -- cannot tell the difference.
 */
#pragma once

#include "api/Stream.h"
#include "api/String.h"

class StreamString : public arduino::Stream, public arduino::String {
public:
    StreamString() { }
    StreamString(const arduino::String &s) : arduino::String(s) { }

    /* Print. Appending to the String IS the write. */
    size_t write(uint8_t c) override {
        return concat((char)c) ? 1 : 0;
    }

    size_t write(const uint8_t *buffer, size_t size) override {
        if (buffer == nullptr || size == 0) {
            return 0;
        }
        /* concat() on a char* takes a length, so an embedded NUL in a
         * response body is kept rather than truncating everything after it.
         * Bodies are not text as far as this class is concerned. */
        return concat((const char *)buffer, size) ? size : 0;
    }

    /* Stream. Reading consumes from the front, which is what makes this
     * usable as a source as well as a sink. */
    int available() override { return (int)length(); }

    int read() override {
        if (length() == 0) {
            return -1;
        }
        const char c = charAt(0);
        remove(0, 1);
        return (uint8_t)c;
    }

    int peek() override {
        if (length() == 0) {
            return -1;
        }
        return (uint8_t)charAt(0);
    }

    void flush() override { }

    using Print::write;
};
