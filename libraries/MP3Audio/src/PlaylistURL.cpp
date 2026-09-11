#include "PlaylistURL.h"

#include <ctype.h>
#include <string.h>

namespace {

/* Content types that name a pointer to the stream rather than the stream. */
const char *const kPlaylistTypes[] = {
    "audio/x-mpegurl",
    "audio/mpegurl",
    "application/vnd.apple.mpegurl",
    "audio/x-scpls",
    "audio/scpls",
    "video/x-ms-asf",
    "audio/x-ms-asx",
    "audio/x-ms-wax",
    "application/xspf+xml",
};

/* The bare type, lowercased: "audio/mpeg; charset=utf-8" -> "audio/mpeg". */
void bareType(const char *in, char *out, size_t cap) {
    size_t n = 0;
    while (*in == ' ' || *in == '\t') { in++; }
    while (*in && *in != ';' && n + 1 < cap) {
        out[n++] = (char)tolower((unsigned char)*in++);
    }
    while (n && (out[n - 1] == ' ' || out[n - 1] == '\t')) { n--; }
    out[n] = '\0';
}

bool isHttpUrl(const char *s) {
    return strncmp(s, "http://", 7) == 0 || strncmp(s, "https://", 8) == 0;
}

/* The value of the first quoted string at or after `from`. .asx attributes
 * use either quote character, so both are accepted. */
bool quotedAfter(const char *line, size_t from, char *out, size_t cap) {
    const char *p = line + from;
    while (*p && *p != '"' && *p != '\'') { p++; }
    if (!*p) { return false; }
    const char q = *p++;
    size_t n = 0;
    while (*p && *p != q && n + 1 < cap) { out[n++] = *p++; }
    out[n] = '\0';
    return n > 0;
}

void trimmed(const char *begin, const char *end, char *out, size_t cap) {
    while (begin < end && isspace((unsigned char)*begin)) { begin++; }
    while (end > begin && isspace((unsigned char)*(end - 1))) { end--; }
    size_t n = (size_t)(end - begin);
    if (n + 1 > cap) { n = cap - 1; }
    memcpy(out, begin, n);
    out[n] = '\0';
}

}  // namespace

namespace PlaylistURL {

bool isAudio(const char *contentType, bool haveIcy) {
    char t[64];
    bareType(contentType ? contentType : "", t, sizeof(t));

    for (size_t i = 0; i < sizeof(kPlaylistTypes) / sizeof(kPlaylistTypes[0]); i++) {
        if (strcmp(t, kPlaylistTypes[i]) == 0) { return false; }
    }
    if (strncmp(t, "audio/", 6) == 0) { return true; }
    /* Servers that call MP3 octet-stream but send icy headers are promising
       Shoutcast audio, and are common enough to accommodate. */
    return haveIcy;
}

bool first(const char *body, size_t len, const char *exclude,
           char *out, size_t outCap) {
    if (!body || !out || outCap == 0) { return false; }

    const char *p = body;
    const char *const end = body + len;

    while (p < end) {
        /* Lines, but ">\n<" too: an .asx often arrives as one long line, and
           splitting between adjacent elements finds the Ref inside it. */
        const char *stop = p;
        while (stop < end && *stop != '\n' && *stop != '\r') {
            if (*stop == '>' && stop + 1 < end && *(stop + 1) == '<') { break; }
            stop++;
        }

        char line[256];
        trimmed(p, stop, line, sizeof(line));

        /* Advance past the separator, whichever it was. */
        p = stop;
        if (p < end && *p == '>') { p++; }
        while (p < end && (*p == '\n' || *p == '\r')) { p++; }

        if (line[0] == '\0' || line[0] == '#') { continue; }

        char low[256];
        size_t i = 0;
        for (; line[i] && i + 1 < sizeof(low); i++) {
            low[i] = (char)tolower((unsigned char)line[i]);
        }
        low[i] = '\0';

        char cand[256];
        cand[0] = '\0';
        const char *href = strstr(low, "href");
        const char *eq = strchr(line, '=');

        if (href) {                       /* .asx, .wax */
            if (!quotedAfter(line, (size_t)(href - low), cand, sizeof(cand))) {
                continue;
            }
        } else if (eq && eq > line) {     /* .pls File1=, [Reference] Ref1= */
            char key[64];
            trimmed(line, eq, key, sizeof(key));
            for (size_t k = 0; key[k]; k++) {
                key[k] = (char)tolower((unsigned char)key[k]);
            }
            if (strncmp(key, "file", 4) == 0 || strncmp(key, "ref", 3) == 0) {
                trimmed(eq + 1, line + strlen(line), cand, sizeof(cand));
            } else {
                /* Not a key we know. Could still be a bare URL containing a
                   query string, so try the whole line. */
                strncpy(cand, line, sizeof(cand) - 1);
                cand[sizeof(cand) - 1] = '\0';
            }
        } else {                          /* .m3u, one bare URL per line */
            strncpy(cand, line, sizeof(cand) - 1);
            cand[sizeof(cand) - 1] = '\0';
        }

        if (cand[0] == '\0' || !isHttpUrl(cand)) { continue; }
        if (exclude && strcmp(cand, exclude) == 0) { continue; }

        strncpy(out, cand, outCap - 1);
        out[outCap - 1] = '\0';
        return true;
    }
    return false;
}

}  // namespace PlaylistURL
