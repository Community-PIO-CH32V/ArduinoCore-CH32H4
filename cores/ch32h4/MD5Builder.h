/* MD5Builder -- the incremental MD5 the Arduino ecosystem expects.
 *
 * WebServer needs this for HTTP digest authentication, which is the only
 * thing in this core that uses MD5 and the only reason it is here.
 *
 * SELF-CONTAINED ON PURPOSE. mbedTLS has an MD5 and so does the PPP corner of
 * lwIP, and neither can be reached from here: TLS is a build option, so a
 * plain-HTTP sketch would not have it, and lwIP's copy is compiled only when
 * PPP is enabled, which it never is. A digest-authenticated web server must
 * not require the TLS stack to be linked, so the implementation lives here.
 *
 * MD5 IS NOT A SECURITY PRIMITIVE ANY MORE and nothing here pretends
 * otherwise. It is used because RFC 2617 digest authentication specifies it;
 * a password sent this way is protected against casual reading of the wire
 * and against nothing else. Anything that matters belongs behind TLS.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include "api/String.h"

class MD5Builder {
public:
    void begin();

    void add(const uint8_t *data, uint16_t len);
    void add(const char *s) { add((const uint8_t *)s, (uint16_t)strlen(s)); }
    void add(const arduino::String &s) {
        add((const uint8_t *)s.c_str(), (uint16_t)s.length());
    }

    /* Finish. add() after this restarts nothing -- call begin() again. */
    void calculate();

    /* The 16 raw bytes. `out` must have room for all of them. */
    void getBytes(uint8_t *out) const;

    /* The digest as 32 lowercase hex characters. */
    arduino::String toString() const;

private:
    void transform(const uint8_t block[64]);

    uint32_t _state[4] = {0, 0, 0, 0};
    uint64_t _bits = 0;          /* message length, in bits */
    uint8_t _buf[64] = {0};      /* partial block */
    size_t _buflen = 0;
    uint8_t _digest[16] = {0};
};
