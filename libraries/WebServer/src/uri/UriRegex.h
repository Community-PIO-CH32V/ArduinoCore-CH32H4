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
#include <regex>

class UriRegex : public Uri {

public:
    explicit UriRegex(const char *uri) : Uri(uri) {};
    explicit UriRegex(const String &uri) : Uri(uri) {};

    Uri* clone() const override final {
        return new UriRegex(_uri);
    };

    void initPathArgs(std::vector<String> &pathArgs) override final {
        std::regex rgx((_uri + "|").c_str());
        std::smatch matches;
        std::string s{""};
        std::regex_search(s, matches, rgx);
        pathArgs.resize(matches.size() - 1);
    }

    bool canHandle(const String &requestUri, std::vector<String> &pathArgs) override final {
        if (Uri::canHandle(requestUri, pathArgs)) {
            return true;
        }

        unsigned int pathArgIndex = 0;
        std::regex rgx(_uri.c_str());
        std::smatch matches;
        std::string s(requestUri.c_str());
        if (std::regex_search(s, matches, rgx)) {
            for (size_t i = 1; i < matches.size(); ++i) {  // skip first
                pathArgs[pathArgIndex] = String(matches[i].str().c_str());
                pathArgIndex++;
            }
            return true;
        }
        return false;
    }
};
