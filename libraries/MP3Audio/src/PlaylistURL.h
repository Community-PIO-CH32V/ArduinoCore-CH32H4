/* Turning a station link into the URL that actually carries audio.
 *
 * Stations rarely hand you the stream. They hand you a playlist, and often a
 * chain of them: one reaches its audio through http, then https, then an .asx,
 * then a Windows Media [Reference] file. Feeding a playlist to an MP3 decoder
 * produces noise, which looks exactly like a broken decoder.
 *
 * This is the parsing half, deliberately separated from the fetching half: it
 * takes bytes and returns a URL, touches no network, and is therefore testable
 * against a handful of synthetic bodies rather than against the internet. The
 * hop loop lives in the sketch, where the HTTP client does.
 *
 * Ported from the MicroPython webradio_i2s.py port for this silicon, formats
 * and quirks alike.
 */
#pragma once

#include <Arduino.h>

namespace PlaylistURL {

/* How many playlists to follow before giving up. Real chains reach three. */
static const int MAX_HOPS = 5;

/* Enough of a playlist to find the first entry in. Stations put it first;
 * reading the whole of a long .m3u to ignore all but one line is waste. */
static const size_t PROBE_BYTES = 1024;

/* Does this response carry the stream itself, rather than a pointer to it?
 *
 * Decided from the content type and NOT from the URL, because the last hop of
 * a chain is routinely a plain path with no telltale extension --
 * `/listen`, `/proxy/radiohayama`, a bare host and port. Judging by the
 * extension gets those wrong in both directions.
 *
 * `haveIcy` covers servers that label MP3 as application/octet-stream but do
 * send icy-metaint or icy-br, which is a promise that what follows is a
 * Shoutcast audio stream.
 */
bool isAudio(const char *contentType, bool haveIcy);

/* The first http(s) URL in a playlist body, or false if this is not a
 * playlist at all.
 *
 * Handles the four formats stations actually use:
 *
 *   .m3u        one bare URL per line
 *   .pls        File1=http://...
 *   .asx/.wax   an element with an href attribute
 *   [Reference] Ref1=http://...
 *
 * Matching is case-insensitive and does not assume tags sit on lines of their
 * own, because spelling and whitespace vary between stations.
 *
 * `exclude` drops a candidate identical to the URL the playlist came from,
 * which is how [Reference] files behave: the first entry points back at
 * itself and only the second goes anywhere. Pass nullptr to skip that.
 */
bool first(const char *body, size_t len, const char *exclude,
           char *out, size_t outCap);

}  // namespace PlaylistURL
