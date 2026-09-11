/* A station link in, a Stream of audio out.
 *
 *     static RadioStream radio;
 *     radio.setInsecure();                  // or setCACert(root_ca)
 *     if (radio.begin("http://play.antenne.de/antenne.m3u")) {
 *         player.begin(radio.stream(), i2s);
 *     }
 *
 * Does the three things that stand between a published link and audio:
 *
 *   - follows redirects: same-scheme ones inside HTTPClient, and the http-to-
 *     https kind it refuses, which is how a station that moved to TLS still
 *     plays from its old published link;
 *   - follows PLAYLISTS, which nothing else here does -- .m3u, .pls, .asx and
 *     Windows Media [Reference], including chains of them, because one station
 *     reaches its audio through http, then https, then an .asx;
 *   - strips Shoutcast metadata, because leaving it in corrupts one frame
 *     every few seconds and sounds exactly like a broken decoder.
 *
 * It exists as a class rather than as example code because the loop is real
 * logic with real edge cases -- a [Reference] file whose first entry points at
 * itself, a server that labels MP3 as octet-stream -- and logic that lives in
 * an example cannot be tested.
 *
 * INCLUDING THIS PULLS IN mbedTLS, because it can speak HTTPS. A sketch that
 * only ever plays from a file or plain HTTP should include MP3Player.h and
 * PlaylistURL.h instead and pay nothing for TLS.
 */
#pragma once

#include <Arduino.h>
#include <HTTPClientSecure.h>

#include "IcyStream.h"
#include "PlaylistURL.h"

class RadioStream {
public:
    /* Verify against this root. Must outlive the connection: the PEM is not
       copied. */
    void setCACert(const char *rootCA) { _ca = rootCA; _insecure = false; }

    /* Skip verification. The connection is encrypted against a passive
       listener and offers nothing against anyone who can answer for the
       station's address. */
    void setInsecure() { _insecure = true; _ca = nullptr; }

    /* Resolve `url` and leave a Stream of audio ready. False on any failure,
       with the reason printed to `log` if one was given. */
    bool begin(const char *url, Print *log = nullptr);
    void end();

    /* Valid between a successful begin() and end(). */
    Stream &stream() { return _icy; }

    /* The URL the audio actually came from, after every hop. */
    const char *url() const { return _url; }
    uint32_t metaint() const { return _metaint; }
    int status() const { return _status; }
    /* Playlists resolved, plus any cross-scheme redirect taken here. Same-
       scheme redirects are followed inside HTTPClient and do not count. */
    int hops() const { return _hops; }

    /* Shoutcast's "now playing", straight from the IcyStream. */
    const char *title() { return _icy.title(); }
    uint32_t titleChanges() { return _icy.titleChanges(); }

private:
    HTTPClientSecure _http;
    IcyStream _icy;
    const char *_ca = nullptr;
    bool _insecure = false;
    char _url[256] = {0};
    uint32_t _metaint = 0;
    int _status = 0;
    int _hops = 0;
};
