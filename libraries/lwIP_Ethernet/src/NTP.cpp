#include "NTP.h"

#include "LwipEthernet.h"

extern "C" {
#include <sys/time.h>
#include "ch32h4_rtc.h"
#include "lwip/apps/sntp.h"
}

/* Set by the callback lwipopts.h points SNTP_SET_SYSTEM_TIME_US at. */
static volatile uint32_t s_last_sync_ms;

extern "C" void ch32h4_sntp_set_time(uint32_t sec, uint32_t us) {
    /* Through settimeofday() rather than straight into the RTC driver, so the
     * one place that knows how to write the counter stays the one place that
     * does. The microseconds are dropped: the counter has one-second
     * resolution, and pretending otherwise would imply an accuracy the
     * hardware does not have. */
    struct timeval tv;
    tv.tv_sec = (time_t)sec;
    tv.tv_usec = (suseconds_t)us;
    if (settimeofday(&tv, nullptr) == 0) {
        s_last_sync_ms = millis();
    }
}

bool NTPClass::begin(const char *server) {
    end();

    /* Poll mode, not listen. A client on a small board behind NAT will never
     * see a broadcast from an upstream server. */
    sntp_setoperatingmode(SNTP_OPMODE_POLL);

    if (server && server[0]) {
        sntp_setservername(0, server);
    } else if (sntp_getservername(0) == nullptr
               && ip_addr_isany(sntp_getserver(0))) {
        /* Nothing configured and DHCP offered nothing. Starting anyway would
         * poll address zero forever and report no error at all. */
        return false;
    }

    sntp_init();
    _running = true;
    return true;
}

bool NTPClass::begin(const IPAddress &server) {
    end();
    sntp_setoperatingmode(SNTP_OPMODE_POLL);

    ip_addr_t addr;
    ip_addr_set_ip4_u32(&addr, (uint32_t)server);
    sntp_setserver(0, &addr);

    sntp_init();
    _running = true;
    return true;
}

void NTPClass::end() {
    if (_running) {
        sntp_stop();
        _running = false;
    }
}

bool NTPClass::synced() const {
    /* The RTC is the authority, not a flag here: a clock set by hand, or one
     * still running from before a reset, is just as synced as one this class
     * set, and a sketch asking "do I know the time" means all three. */
    return ch32h4_rtc_is_set();
}

uint32_t NTPClass::lastSyncMillis() const {
    return s_last_sync_ms;
}

bool NTPClass::waitSynced(uint32_t timeout_ms) {
    const uint32_t start = millis();
    while (!synced()) {
        if ((millis() - start) >= timeout_ms) {
            return false;
        }
        /* The reply arrives through lwIP's timers, which run from
         * Ethernet::update(), which runs from yield(). Waiting without this
         * cannot ever succeed. */
        yield();
    }
    return true;
}

bool NTPClass::waitAnswer(uint32_t timeout_ms) {
    const uint32_t before = s_last_sync_ms;
    const uint32_t start = millis();
    while (s_last_sync_ms == before) {
        if ((millis() - start) >= timeout_ms) {
            return false;
        }
        yield();
    }
    return true;
}

NTPClass NTP;
