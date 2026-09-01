/* SNTP -- setting the clock from the network.
 *
 * This is what makes the rest of the time-dependent machinery usable. File
 * timestamps come from the RTC through FatFs' get_fattime(); TLS certificate
 * validity is checked against it too, and a board that thinks it is the year
 * 2000 rejects every certificate ever issued with an error that says nothing
 * about the clock. A board with no battery has no idea what time it is until
 * something tells it, and this is that something.
 *
 *     NTP.begin("pool.ntp.org");
 *     if (NTP.waitSynced(15000)) {  ...  }
 *
 * lwIP's SNTP client does the work; this is the Arduino face on it, plus the
 * one thing lwIP does not provide -- a way to wait for the first answer.
 */
#pragma once

#include <Arduino.h>
#include <IPAddress.h>

class NTPClass {
public:
    /* Start the client. `server` may be a name or a dotted address; a null or
     * empty one uses whatever DHCP offered, if anything.
     *
     * Returns immediately. The first answer arrives through lwIP's timers,
     * which run from yield(), so a sketch has to keep yielding -- waitSynced()
     * does that for you. */
    bool begin(const char *server = nullptr);
    bool begin(const IPAddress &server);
    void end();

    /* Block until the clock is KNOWN, pumping the stack meanwhile.
     *
     * Returns immediately -- and true -- if the clock was already set, by
     * hand, by a previous sync, or because it kept running through a reset on
     * backup power. That is what a sketch wants: it is asking whether it can
     * trust the time, and blocking for fifteen seconds to re-learn something
     * it already knows helps nobody.
     *
     * Returns false on timeout, which is the normal outcome with no route to
     * the internet -- callers must handle it rather than assume a clock. */
    bool waitSynced(uint32_t timeout_ms = 15000);

    /* Block until an SNTP answer actually arrives.
     *
     * Different from waitSynced() in exactly the case that matters when you
     * are checking whether the network works: this ignores a clock that was
     * already right and waits for a packet. A test that used waitSynced()
     * against an already-set clock passes in three milliseconds without a
     * single datagram leaving the board. */
    bool waitAnswer(uint32_t timeout_ms = 15000);

    /* True once an answer has been accepted. Distinct from the RTC merely
     * running: see ch32h4_rtc_is_set(). */
    bool synced() const;

    /* When the last answer was accepted, in millis(). Zero if never. Lets a
     * sketch notice that the clock has not been refreshed in a long time. */
    uint32_t lastSyncMillis() const;

    bool running() const { return _running; }

private:
    bool _running = false;
};

extern NTPClass NTP;
