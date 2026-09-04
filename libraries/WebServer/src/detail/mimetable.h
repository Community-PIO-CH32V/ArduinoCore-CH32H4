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

#include <api/String.h>

namespace mime {

enum type {
    html,
    htm,
    css,
    txt,
    js,
    json,
    png,
    gif,
    jpg,
    ico,
    svg,
    ttf,
    otf,
    woff,
    woff2,
    eot,
    sfnt,
    xml,
    pdf,
    zip,
    gz,
    appcache,
    none,
    maxType
};

struct Entry {
    const char *endsWith;
    const char *mimeType;
};

extern const Entry mimeTable[maxType];

arduino::String getContentType(const arduino::String& path);

}
