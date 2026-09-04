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

class UriBraces : public Uri {

public:
    explicit UriBraces(const char *uri) : Uri(uri) {};
    explicit UriBraces(const String &uri) : Uri(uri) {};

    Uri* clone() const override final {
        return new UriBraces(_uri);
    };

    void initPathArgs(std::vector<String> &pathArgs) override final {
        int numParams = 0, start = 0;
        do {
            start = _uri.indexOf("{}", start);
            if (start > 0) {
                numParams++;
                start += 2;
            }
        } while (start > 0);
        pathArgs.resize(numParams);
    }

    bool canHandle(const String &requestUri, std::vector<String> &pathArgs) override final {
        if (Uri::canHandle(requestUri, pathArgs)) {
            return true;
        }

        size_t uriLength = _uri.length();
        unsigned int pathArgIndex = 0;
        unsigned int requestUriIndex = 0;
        for (unsigned int i = 0; i < uriLength; i++, requestUriIndex++) {
            char uriChar = _uri[i];
            char requestUriChar = requestUri[requestUriIndex];

            if (uriChar == requestUriChar) {
                continue;
            }
            if (uriChar != '{') {
                return false;
            }

            i += 2; // index of char after '}'
            if (i >= uriLength) {
                // there is no char after '}'
                pathArgs[pathArgIndex] = requestUri.substring(requestUriIndex);
                return pathArgs[pathArgIndex].indexOf("/") == -1; // path argument may not contain a '/'
            } else {
                char charEnd = _uri[i];
                int uriIndex = requestUri.indexOf(charEnd, requestUriIndex);
                if (uriIndex < 0) {
                    return false;
                }
                pathArgs[pathArgIndex] = requestUri.substring(requestUriIndex, uriIndex);
                requestUriIndex = (unsigned int) uriIndex;
            }
            pathArgIndex++;
        }

        return requestUriIndex >= requestUri.length();
    }
};
