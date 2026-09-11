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
    /* THESE RECORD, THEY DO NOT CONNECT OR ALLOCATE.
     *
     * Settings live on this object, not on a client, and are applied to
     * whatever TLS client begin() ends up building. Three things follow, and
     * the first one used to be a real trap.
     *
     * Calling one of these no longer creates a client. When they did, a
     * sketch that configured TLS up front and then fetched an http:// URL got
     * the TLS client back out of begin(), which opened a plain socket to port
     * 80 and began a handshake with a server speaking HTTP. It failed as a
     * connection error, mentioned nothing about certificates, and worked
     * against https:// -- so the configuration looked correct. begin() now
     * replaces a client whose kind does not match the scheme, and it can only
     * do that safely because the settings are here to re-apply.
     *
     * They also work in any order relative to begin(), and calling one after
     * a connection is open takes effect on the next one.
     *
     * As before, the PEM strings are not copied and must outlive this object.
     */
    void setInsecure() {
        _insecure = true;
        _ca = nullptr;
        _configure();
    }
    void setCACert(const char *rootCA) {
        _ca = rootCA;
        _insecure = false;
        _configure();
    }
    void setCertificate(const char *client_ca) {
        _cert = client_ca;
        _configure();
    }
    void setPrivateKey(const char *private_key) {
        _key = private_key;
        _configure();
    }

protected:
    /* What begin("https://...") calls on the base class. */
    Client *_makeSecureClient() override {
        return _tls();
    }

    EthernetClientSecure *_tls() {
        /* !_clientTLS, not just !_clientMade. A plain EthernetClient made for
           an earlier http:// URL is not one of these, and casting it would
           call mbedTLS methods on an object that has no session -- a crash
           rather than a wrong answer. */
        if (!_clientMade || !_clientTLS) {
            if (_clientMade) {
                _destroyMade();
            }
            _clientMade = new EthernetClientSecure();
            /* The deleter, captured where the concrete type is still known --
               arduino::Client has no virtual destructor, so deleting through
               a Client* would leak the whole mbedTLS session. */
            _deleteMade = [](Client *c) {
                delete static_cast<EthernetClientSecure *>(c);
            };
            _clientGiven = false;
            _clientTLS = true;
            _configure();
        }
        return (EthernetClientSecure *)_clientMade;
    }

    /* Push the stored settings onto the TLS client, if one exists yet. */
    void _configure() {
        if (!_clientMade || !_clientTLS) {
            return;
        }
        EthernetClientSecure *tls = (EthernetClientSecure *)_clientMade;
        if (_insecure) {
            tls->setInsecure();
        } else if (_ca) {
            tls->setCACert(_ca);
        }
        if (_cert) {
            tls->setCertificate(_cert);
        }
        if (_key) {
            tls->setPrivateKey(_key);
        }
    }

    const char *_ca = nullptr;
    const char *_cert = nullptr;
    const char *_key = nullptr;
    bool _insecure = false;
};
