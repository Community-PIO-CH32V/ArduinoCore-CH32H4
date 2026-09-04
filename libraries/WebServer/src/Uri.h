/* PORTED FOR THE CH32H41x ARDUINO CORE.
 *
 * Taken from arduino-pico, which took it from the esp8266 core. The only
 * systematic change is that WiFiClient has become arduino::Client: this code
 * only ever used a client through connected(), available(), read(), write()
 * and stop(), all of which are on the Client interface, so naming that
 * interface instead of one implementation of it is what makes the library
 * work over Ethernet here. WiFiServer likewise becomes the server type the
 * template is instantiated with.
 *
 * Licence unchanged: LGPL-2.1-or-later, as below.
 */
#pragma once

#include <Arduino.h>
#include <vector>

class Uri {

protected:
    const String _uri;

public:
    Uri(const char *uri) : _uri(uri) {}
    Uri(const String &uri) : _uri(uri) {}
    Uri(const __FlashStringHelper *uri) : _uri(String(uri)) {}
    virtual ~Uri() {}

    virtual Uri* clone() const {
        return new Uri(_uri);
    };

    virtual void initPathArgs(__attribute__((unused)) std::vector<String> &pathArgs) {}

    virtual bool canHandle(const String &requestUri, __attribute__((unused)) std::vector<String> &pathArgs) {
        return _uri == requestUri;
    }
};
