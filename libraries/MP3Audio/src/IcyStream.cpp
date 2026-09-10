#include "IcyStream.h"

#include <string.h>

int IcyStream::available() {
    if (_metaint == 0) { return _up.available(); }
    if (_untilMeta == 0) { consumeMetadata(); }
    const int a = _up.available();
    /* Never promise past the next metadata block: a caller that reads the
       whole of available() in one go must not swallow the length byte. */
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
 * then rearms. If the length byte has not arrived yet this returns having done
 * nothing and is retried on the next call, so a socket that delivers the
 * boundary in two pieces does not lose sync. */
void IcyStream::consumeMetadata() {
    const int lenByte = _up.read();
    if (lenByte < 0) { return; }
    _untilMeta = _metaint;

    size_t n = (size_t)lenByte * 16u;
    if (n == 0) { return; }        /* the common case: no change since the last */

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
    /* Shoutcast repeats the same title in every block, so only a change is
       reported -- otherwise titleChanges() would count blocks, not songs. */
    if (tl == strlen(_title) && strncmp(_title, s, tl) == 0) { return; }
    memcpy(_title, s, tl);
    _title[tl] = '\0';
    _titleChanges++;
}
