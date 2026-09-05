/* HTTPClient, over TLS.
 *
 *     #include <HTTPClientSecure.h>
 *
 *     HTTPClientSecure http;
 *     http.setCACert(root_ca_pem);
 *     if (http.begin("https://example.com/")) { int rc = http.GET(); }
 *
 * A separate header from HTTPClient.h, and a separate class, for one reason:
 * including this is what pulls mbedTLS into the build. A sketch that speaks
 * plain HTTP includes HTTPClient.h and pays nothing -- no quarter-megabyte
 * library, and none of its compile time.
 *
 * ESP32 puts these four methods on HTTPClient itself, because the ESP-IDF has
 * mbedTLS in it whether you asked or not. Here the dependency is real, so it
 * is visible. Everything else -- begin(), GET(), POST(), the headers, the
 * cookie jar -- is inherited unchanged, and an https:// URL passed to
 * begin() builds the secure client for you.
 *
 * The alternative, which needs neither this header nor this class: build an
 * EthernetClientSecure yourself, configure it, and hand it to
 * begin(client, url). That is the ESP32 escape hatch too, and it is what to
 * use when you want more control than the four setters below.
 */
#pragma once

#include <EthernetClientSecure.h>

#include "HTTPClient.h"

class HTTPClientSecure : public HTTPClient {
public:
    /* The subset ESP32's HTTPClient exposes, because it is the subset this
     * core's TLS can back: EthernetClientSecure is mbedTLS, not BearSSL.
     * arduino-pico forwards a much larger set -- setSession, setTrustAnchors,
     * setKnownKey, setCertStore, setCiphers, setSSLVersion -- and every one of
     * those takes a BearSSL type. They are not stubbed out here: a method that
     * accepts a certificate store and quietly ignores it is worse than one
     * that does not exist, because the sketch still looks like it validates. */
    void setInsecure() {
        _tls()->setInsecure();
    }
    void setCACert(const char *rootCA) {
        _tls()->setCACert(rootCA);
    }
    void setCertificate(const char *client_ca) {
        _tls()->setCertificate(client_ca);
    }
    void setPrivateKey(const char *private_key) {
        _tls()->setPrivateKey(private_key);
    }

protected:
    /* What begin("https://...") calls on the base class. */
    Client *_makeSecureClient() override {
        return _tls();
    }

    EthernetClientSecure *_tls() {
        if (!_clientMade) {
            _clientMade = new EthernetClientSecure();
            /* The deleter, captured where the concrete type is still known --
               arduino::Client has no virtual destructor, so deleting through
               a Client* would leak the whole mbedTLS session. */
            _deleteMade = [](Client *c) {
                delete static_cast<EthernetClientSecure *>(c);
            };
            _clientGiven = false;
        }
        _clientTLS = true;
        return (EthernetClientSecure *)_clientMade;
    }
};
