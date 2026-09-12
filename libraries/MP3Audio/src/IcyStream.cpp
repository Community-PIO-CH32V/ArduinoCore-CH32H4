#include "IcyStream.h"

#include <string.h>

int IcyStream::available() {
    if (!_up) { return 0; }
    if (_metaint == 0) { return _up->available(); }
    if (_untilMeta == 0) { consumeMetadata(); }
    const int a = _up->available();
    /* Never promise past the next metadata block: a caller that reads the
       whole of available() in one go must not swallow the length byte. */
    return a < (int)_untilMeta ? a : (int)_untilMeta;
}

int IcyStream::read() {
    if (!_up) { return -1; }
    if (_metaint == 0) { return _up->read(); }
    if (_untilMeta == 0) { consumeMetadata(); }
    const int c = _up->read();
    if (c >= 0 && _untilMeta > 0) { _untilMeta--; }
    return c;
}

/* Called with the audio counter at zero. Reads the length byte and the block,
 * then rearms. If the length byte has not arrived yet this returns having done
 * nothing and is retried on the next call, so a socket that delivers the
 * boundary in two pieces does not lose sync. */
void IcyStream::consumeMetadata() {
    /* RESUMABLE, because the block can arrive in pieces.
     *
     * Two states, distinguished by _metaLeft: zero means the next byte is the
     * length byte, non-zero means that many text bytes are still owed. The
     * audio counter is rearmed only when the block is complete, so a caller
     * that gets nothing this time simply asks again. */
    if (_metaLeft == 0) {
        const int lenByte = _up->read();
        if (lenByte < 0) { return; }          /* not even the length yet */
        _metaLeft = (uint32_t)lenByte * 16u;
        _metaGot = 0;
        if (_metaLeft == 0) {
            _untilMeta = _metaint;            /* common case: no change */
            return;
        }
    }

    while (_metaLeft) {
        const int c = _up->read();
        if (c < 0) {
            /* Mid-block. Everything stays as it is and this is retried; the
               counter is deliberately still zero, so available() reports
               nothing rather than handing metadata out as audio. */
            return;
        }
        _metaLeft--;
        /* A block may be far longer than any title. The excess is counted and
           discarded, which is what keeps the byte accounting exact. */
        if (_metaGot + 1u < sizeof(_metaBuf)) {
            _metaBuf[_metaGot++] = (char)c;
        }
    }

    _metaBuf[_metaGot] = '\0';
    _untilMeta = _metaint;                    /* block complete: back to audio */

    /* StreamTitle='...'; is the only field anyone cares about. */
    const char *s = strstr(_metaBuf, "StreamTitle='");
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
