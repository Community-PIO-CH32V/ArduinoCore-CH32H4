#include "RadioStream.h"

#include <string.h>

void RadioStream::end() {
    _http.end();
    _metaint = 0;
    _status = 0;
}

bool RadioStream::begin(const char *url, Print *log) {
    if (!url) { return false; }
    strncpy(_url, url, sizeof(_url) - 1);
    _url[sizeof(_url) - 1] = '\0';
    _metaint = 0;
    _status = 0;

    for (_hops = 0; _hops < PlaylistURL::MAX_HOPS; _hops++) {
        /* TLS is configured per hop, and ONLY for https hops.
         *
         * Touching setInsecure() or setCACert() instantiates the secure
         * client, and HTTPClient::begin(url) only creates one `if
         * (!_client())` -- so a TLS client configured up front gets reused for
         * an http:// URL and attempts a handshake against a plain HTTP server.
         * That fails as a connection error, which reads like the station being
         * down. A chain that starts at http:// and ends at https:// hits this
         * on its first hop. See docs/hazards.md. */
        const bool https = strncmp(_url, "https://", 8) == 0;
        if (https) {
            if (_insecure) {
                _http.setInsecure();
            } else if (_ca) {
                _http.setCACert(_ca);
            }
        }
        /* A redirect is the station's other way of moving you. HTTPClient
           follows those, and they are not counted as playlist hops. */
        _http.setFollowRedirects(HTTPC_STRICT_FOLLOW_REDIRECTS);

        if (!_http.begin(_url)) {
            if (log) { log->print("cannot parse URL: "); log->println(_url); }
            return false;
        }

        /* BEFORE GET(): HTTPClient keeps only the headers it is told to keep.
           A metaint silently reading zero turns metadata into corrupt audio,
           and Content-Type is what distinguishes audio from a pointer to it. */
        static const char *keys[] = { "icy-metaint", "Content-Type", "icy-br",
                                      "Location" };
        _http.collectHeaders(keys, 4);

        /* Metadata is opt-in: a Shoutcast server sends icy-metaint, and the
           interleaved title blocks that go with it, only to a client that asks
           for them. Without this the audio still plays but title() stays empty
           forever, which looks like broken parsing rather than a request that
           was never made. Sent on playlist hops too, where servers ignore it.

           IcyStream is what removes those blocks again. Asking for metadata
           without stripping it would corrupt one frame every few seconds. */
        _http.addHeader("Icy-MetaData", "1");

        _status = _http.GET();

        /* A redirect that HTTPClient would not follow itself.
         *
         * It follows same-scheme ones internally, but setURL() refuses a
         * Location whose scheme differs from the current one -- rightly, since
         * the client object is already the wrong kind and connecting a plain
         * socket to port 443 would send the request in the clear. So the 3xx
         * arrives here instead, and http:// stations that moved to https are
         * common enough that failing on one would look like the station being
         * down. Taking it as a hop is the same "go here instead" a playlist
         * entry is, and end()-then-begin() builds the right client for the new
         * scheme. */
        if (_status >= 300 && _status < 400) {
            const String loc = _http.header("Location");
            if (loc.startsWith("http://") || loc.startsWith("https://")) {
                if (log) {
                    log->print("  "); log->print(_status);
                    log->print(" -> "); log->println(loc);
                }
                _http.end();
                strncpy(_url, loc.c_str(), sizeof(_url) - 1);
                _url[sizeof(_url) - 1] = '\0';
                continue;
            }
        }

        if (_status != 200) {
            if (log) {
                log->print("GET "); log->print(_url);
                log->print(" -> "); log->println(_status);
            }
            _http.end();
            return false;
        }

        _metaint = (uint32_t)_http.header("icy-metaint").toInt();
        const bool haveIcy = _metaint > 0 || _http.header("icy-br").length() > 0;
        const String ctype = _http.header("Content-Type");

        if (PlaylistURL::isAudio(ctype.c_str(), haveIcy)) {
            _icy.begin(_http.getStream(), _metaint);
            if (log) {
                log->print("stream "); log->print(_url);
                log->print("  type ");
                log->print(ctype.length() ? ctype : String("none"));
                log->print("  metaint "); log->println(_metaint);
            }
            return true;
        }

        /* A playlist. Read it, find the first entry, go again.
         *
         * getString() rather than readBytes() on getStream(): the latter
         * ignores Content-Length and waits for as many bytes as the buffer
         * holds, so a 60-byte playlist either comes back empty or stalls for
         * the stream timeout depending on when the server closes. It also
         * keeps a PROBE_BYTES buffer off this function's stack. */
        const String body = _http.getString();
        _http.end();

        char next[sizeof(_url)];
        if (log) {
            log->print("  playlist body ");
            log->print(body.length());
            log->println(" bytes");
        }
        /* The current URL is excluded because a [Reference] file's first entry
           points back at itself; following it would loop until the hop limit
           and report the wrong reason. */
        if (!PlaylistURL::first(body.c_str(), body.length(), _url,
                                next, sizeof(next))) {
            if (log) {
                log->print(_url);
                log->print(" is neither audio nor a playlist (type ");
                log->print(ctype.length() ? ctype : String("none"));
                log->println(")");
            }
            return false;
        }
        if (log) { log->print("  playlist -> "); log->println(next); }
        memcpy(_url, next, sizeof(_url));
    }

    if (log) {
        log->print("gave up after ");
        log->print(PlaylistURL::MAX_HOPS);
        log->println(" hops -- playlists or redirects going in a circle");
    }
    _http.end();
    return false;
}
