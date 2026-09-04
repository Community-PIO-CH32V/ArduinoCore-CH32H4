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

#include "Uri.h"
#include <fnmatch.h>

class UriGlob : public Uri {

public:
    explicit UriGlob(const char *uri) : Uri(uri) {};
    explicit UriGlob(const String &uri) : Uri(uri) {};

    Uri* clone() const override final {
        return new UriGlob(_uri);
    };

    bool canHandle(const String &requestUri, __attribute__((unused)) std::vector<String> &pathArgs) override final {
        return fnmatch(_uri.c_str(), requestUri.c_str(), 0) == 0;
    }
};
