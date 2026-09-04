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
#include <Arduino.h>
#include "mimetable.h"
#include "pgmspace.h"

namespace mime {

// Table of extension->MIME strings stored in PROGMEM, needs to be global due to GCC section typing rules
const Entry mimeTable[maxType] = {
    { ".html", "text/html" },
    { ".htm", "text/html" },
    { ".css", "text/css" },
    { ".txt", "text/plain" },
    { ".js", "application/javascript" },
    { ".json", "application/json" },
    { ".png", "image/png" },
    { ".gif", "image/gif" },
    { ".jpg", "image/jpeg" },
    { ".ico", "image/x-icon" },
    { ".svg", "image/svg+xml" },
    { ".ttf", "application/x-font-ttf" },
    { ".otf", "application/x-font-opentype" },
    { ".woff", "application/font-woff" },
    { ".woff2", "application/font-woff2" },
    { ".eot", "application/vnd.ms-fontobject" },
    { ".sfnt", "application/font-sfnt" },
    { ".xml", "text/xml" },
    { ".pdf", "application/pdf" },
    { ".zip", "application/zip" },
    { ".gz", "application/x-gzip" },
    { ".appcache", "text/cache-manifest" },
    { "", "application/octet-stream" }
};


arduino::String getContentType(const arduino::String& path) {
    for (size_t i = 0; i < maxType; i++) {
        if (path.endsWith(FPSTR(mimeTable[i].endsWith))) {
            return arduino::String(FPSTR(mimeTable[i].mimeType));
        }
    }
    // Fall-through and just return default type
    return arduino::String(FPSTR(mimeTable[none].mimeType));
}
}
