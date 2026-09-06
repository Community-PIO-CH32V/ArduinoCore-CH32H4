/* The timestamp FatFs stamps into every directory entry it writes.
 *
 * Here rather than in a disk driver because it belongs to no volume: files on
 * the internal flash and files on an SD card get their time from the same
 * clock, and FatFs asks for it once per write regardless of which volume the
 * write is going to.
 *
 * It goes through time(), so a sketch that has started the RTC and synced it
 * -- from SNTP, or by hand -- gets real timestamps on its files without
 * anything here having to know where the time came from.
 *
 * When the clock has not been set, this returns the fixed FF_NORTC_* date
 * rather than 1980-01-01. Both are wrong, but a file dated 1980 looks like a
 * corrupt directory entry, where one dated at the FF_NORTC_YEAR the config
 * names is obviously a default.
 */
#include "ff.h"

#include <time.h>

#include "ch32h4_rtc.h"

DWORD get_fattime(void) {
    if (ch32h4_rtc_is_set()) {
        time_t now = time(NULL);
        struct tm tmv;
        if (gmtime_r(&now, &tmv) != NULL && tmv.tm_year >= 80) {
            return ((DWORD)(tmv.tm_year - 80) << 25)
                   | ((DWORD)(tmv.tm_mon + 1) << 21)
                   | ((DWORD)tmv.tm_mday << 16)
                   | ((DWORD)tmv.tm_hour << 11)
                   | ((DWORD)tmv.tm_min << 5)
                   | ((DWORD)(tmv.tm_sec / 2));   /* two-second resolution */
        }
    }
    return ((DWORD)(FF_NORTC_YEAR - 1980) << 25)
           | ((DWORD)FF_NORTC_MON << 21)
           | ((DWORD)FF_NORTC_MDAY << 16);
}
