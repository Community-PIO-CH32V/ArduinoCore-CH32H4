/* The real-time clock, and the C library's idea of the time.
 *
 * Ported from the MicroPython port for this silicon. Almost every comment here
 * describes something that failed on a bench first; the register sequences in
 * particular are not interchangeable with the order the SDK's examples use.
 *
 * --- the epoch -----------------------------------------------------------
 *
 * The counter holds seconds since 2000-01-01 00:00:00 UTC, unsigned.
 *
 * WCH's own RTC example stores a Unix timestamp there and converts it with a
 * signed time_t, which stops working on 19 January 2038 (openwch/ch32h417
 * issue 11). Holding an unsigned count from 2000 instead runs to 2136, and
 * needs no offset table, no overflow interrupt and no shadow copy -- the
 * signedness was the whole bug, and fixing that is worth more than re-basing
 * the epoch to buy a few decades. Conversion to and from the Unix epoch is one
 * addition, here.
 */
#include <string.h>

#include "Arduino.h"
#include "ch32h417.h"
#include "ch32h4_rtc.h"

/* Ticks per second of each source. The prescaler register takes this minus
 * one: the reference manual gives the division factor as PRL[19:0] + 1.
 *
 * HSE/512 is 48828.125 Hz and cannot be expressed here. 48828 is the closer of
 * the two whole numbers -- it makes the second 2.6 ppm short, where 48829
 * would make it 18 ppm long. */
#define RTC_LSE_HZ   32768u
#define RTC_LSI_HZ   40000u
#define RTC_HSE_HZ   (HSE_VALUE / 512u)

/* How long to give an oscillator to start. The LSE is a watch crystal and
 * takes a few hundred milliseconds; this is generous enough to cover a cold
 * one and short enough that a board with no crystal fitted is not left
 * hanging. */
#define RTC_STARTUP_MS  1500u

/* RTOFF and RSF both clear within a few RTC clock cycles, so this is three
 * orders of magnitude of slack. It exists because the register waits must not
 * be able to hang -- see rtc_wait_flag(). */
#define RTC_REG_MS      100u

/* Seconds between 1970-01-01 and 2000-01-01. */
#define RTC_EPOCH_OFFSET  946684800u

/* The counter value below which the clock is considered unset.
 *
 * This part has no backup data registers -- RCC has a clock-enable bit for a
 * BKP block, but the SDK defines no register map for one and MicroPython's
 * driver does not use it either -- so there is nowhere to keep a "the time has
 * been set" flag that survives a reset alongside the counter.
 *
 * The counter itself answers the question well enough. It starts at zero,
 * which is 2000-01-01, so a clock nobody has set reads as a date a quarter of
 * a century ago. Anything past 2024 was put there by someone. The failure this
 * cannot catch -- a board powered continuously since 2000 whose counter has
 * ticked up here on its own -- is not a case worth carrying state for.
 *
 * 757382400 = 2024-01-01 00:00:00 UTC, in counter units. */
#define RTC_SET_THRESHOLD  757382400u

static uint8_t s_source;
static uint32_t s_hz;

static bool timed_out(uint32_t deadline) {
    return (int32_t)(millis() - deadline) > 0;
}

/* Read-modify-write BDCTLR and confirm it took. Returns false if it never
 * does.
 *
 * Necessary because the backup domain runs from a much slower clock than the
 * 400 MHz core, and a write that arrives while the previous one is still
 * crossing is dropped without any indication. WCH's own RTC example gestures
 * at this with a bare NOP delay inserted only for the non-V3F cores. A fixed
 * delay is a guess; reading back until the value is there is not.
 *
 * Getting this wrong is not a transient glitch. The pair of writes that pulses
 * BDRST silently lost its second half, which left the backup domain held in
 * reset -- and with it held, every later write is dropped too, so the clock
 * could not be started again by anything short of a power cycle. */
static bool bdctlr_write(uint32_t clear, uint32_t set) {
    uint32_t deadline = millis() + RTC_REG_MS;
    for (;;) {
        RCC->BDCTLR = (RCC->BDCTLR & ~clear) | set;
        uint32_t now = RCC->BDCTLR;
        if ((now & clear & ~set) == 0 && (now & set) == set) {
            return true;
        }
        if (timed_out(deadline)) {
            return false;
        }
    }
}

/* The SDK's RTC_WaitForLastTask() and RTC_WaitForSynchro() spin with no bound,
 * and both wait on flags driven by the RTC clock domain. If no oscillator is
 * actually driving that domain they never return -- which at boot is a board
 * that hangs and says nothing about why. Every wait here is bounded. */
static bool rtc_wait_flag(uint16_t flag) {
    uint32_t deadline = millis() + RTC_REG_MS;
    while (!(RTC->CTLRL & flag)) {
        if (timed_out(deadline)) {
            return false;
        }
    }
    return true;
}

/* RTOFF: the last write has reached the RTC clock domain and another may
 * start. Every write to CNT or PSCR has to be followed by this. */
static bool rtc_wait_write(void) {
    return rtc_wait_flag(RTC_FLAG_RTOFF);
}

/* RSF: the APB-side copies of CNT and DIV have been refreshed from the RTC
 * clock domain. Required after any reset of the APB interface, because until
 * it sets, reads return whatever was latched before. */
static bool rtc_wait_sync(void) {
    RTC->CTLRL &= (uint16_t)~RTC_FLAG_RSF;
    return rtc_wait_flag(RTC_FLAG_RSF);
}

static uint32_t source_hz(uint8_t source) {
    switch (source) {
        case CH32H4_RTC_SRC_LSE: return RTC_LSE_HZ;
        case CH32H4_RTC_SRC_LSI: return RTC_LSI_HZ;
        default:                 return RTC_HSE_HZ;
    }
}

ch32h4_rtc_src_t ch32h4_rtc_source(void) {
    uint32_t bd = RCC->BDCTLR;
    if (!(bd & RCC_RTCEN)) {
        return CH32H4_RTC_SRC_NONE;
    }
    switch (bd & RCC_RTCSEL) {
        case RCC_RTCCLKSource_LSE:        return CH32H4_RTC_SRC_LSE;
        case RCC_RTCCLKSource_LSI:        return CH32H4_RTC_SRC_LSI;
        case RCC_RTCCLKSource_HSE_Div512: return CH32H4_RTC_SRC_HSE;
        default:                          return CH32H4_RTC_SRC_NONE;
    }
}

uint32_t ch32h4_rtc_hz(void) { return s_hz; }
bool ch32h4_rtc_running(void) { return s_source != CH32H4_RTC_SRC_NONE; }

static bool start_oscillator(uint8_t source) {
    if (source == CH32H4_RTC_SRC_HSE) {
        /* Already running: it is what the PLL is locked to. */
        return RCC_GetFlagStatus(RCC_FLAG_HSERDY) != RESET;
    }
    if (source == CH32H4_RTC_SRC_LSI) {
        /* LSI lives in RCC_CTLR, outside the backup domain, so it needs none
         * of the care above. */
        RCC_LSICmd(ENABLE);
    } else if (!bdctlr_write(RCC_LSEBYP, RCC_LSEON)) {
        return false;
    }
    uint8_t flag = (source == CH32H4_RTC_SRC_LSI) ? RCC_FLAG_LSIRDY
                                                  : RCC_FLAG_LSERDY;
    uint32_t deadline = millis() + RTC_STARTUP_MS;
    while (RCC_GetFlagStatus(flag) == RESET) {
        if (timed_out(deadline)) {
            return false;
        }
    }
    return true;
}

static bool configure_inner(uint8_t source) {
    RCC_HB1PeriphClockCmd(RCC_HB1Periph_PWR | RCC_HB1Periph_BKP, ENABLE);
    (void)RCC->HB1PCENR;
    PWR_BackupAccessCmd(ENABLE);

    if ((uint8_t)ch32h4_rtc_source() == source) {
        /* Already ticking from the right oscillator, which is the normal case
         * after a warm reset. Leave the counter and prescaler alone: rewriting
         * them would throw away a clock that has been keeping time since
         * before this firmware started. */
        if (!rtc_wait_sync()) {
            return false;
        }
        s_source = source;
        s_hz = source_hz(source);
        return true;
    }

    /* Selecting a source needs a full backup-domain reset pulse first, and
     * this is the part that is easy to get wrong on this chip.
     *
     * RTCSEL cannot be *changed* once written; only a backup-domain reset
     * reopens it. Two things then conspire. This part comes out of power-on
     * with BDRST already asserted, unlike STM32, so every write to BDCTLR is
     * silently dropped until it is cleared -- and RCC_LSEConfig() writes a
     * whole byte to BDCTLR, RTCSEL bits included, so merely starting the LSE
     * counts as writing RTCSEL=00 and latches it there. After that the clock
     * can never be selected, every write reads back as zero, and nothing
     * reports an error. Pulsing BDRST here clears both the latch and the
     * power-on assertion, and costs nothing: the only time on the clock at
     * this point is time this call was going to replace anyway. */
    if (!bdctlr_write(0, RCC_BDRST) || !bdctlr_write(RCC_BDRST, 0)) {
        return false;
    }
    PWR_BackupAccessCmd(ENABLE);

    /* Oscillator first, selection second: starting the LSE writes BDCTLR, and
     * the SDK's RCC_LSEConfig() rewrites the whole low byte including RTCSEL,
     * so the other order would clear the selection just made. */
    if (!start_oscillator(source)) {
        return false;
    }

    uint32_t sel = source == CH32H4_RTC_SRC_LSE ? RCC_RTCCLKSource_LSE
                 : source == CH32H4_RTC_SRC_LSI ? RCC_RTCCLKSource_LSI
                                                : RCC_RTCCLKSource_HSE_Div512;
    if (!bdctlr_write(RCC_RTCSEL, sel) || !bdctlr_write(0, RCC_RTCEN)) {
        return false;
    }

    if (!rtc_wait_write() || !rtc_wait_sync() || !rtc_wait_write()) {
        return false;
    }
    RTC_SetPrescaler(source_hz(source) - 1);
    if (!rtc_wait_write()) {
        return false;
    }

    s_source = source;
    s_hz = source_hz(source);
    return true;
}

bool ch32h4_rtc_begin(ch32h4_rtc_src_t src) {
    if (src == CH32H4_RTC_SRC_NONE) {
        return false;
    }
    if (configure_inner((uint8_t)src)) {
        return true;
    }
    /* A failed attempt must not leave the board worse off than it found it. An
     * abandoned attempt used to leave BDRST asserted, which made every
     * subsequent attempt fail too, so one bad call bricked the RTC until the
     * next power cycle. */
    s_source = CH32H4_RTC_SRC_NONE;
    s_hz = 0;
    bdctlr_write(RCC_BDRST, 0);
    return false;
}

uint32_t ch32h4_rtc_ticks(void) {
    return s_source ? RTC_GetCounter() : 0;
}

bool ch32h4_rtc_get(time_t *seconds, uint32_t *microseconds) {
    if (s_source == CH32H4_RTC_SRC_NONE) {
        return false;
    }
    /* CNT and DIV are separate registers ticking off the same oscillator, so
     * they have to be sampled either side of the counter to know they belong
     * to the same second. Without this, a read landing on the boundary pairs a
     * new second with the old fraction and time appears to jump backwards by
     * almost a second. */
    uint32_t cnt, div, again;
    do {
        cnt = RTC_GetCounter();
        div = RTC_GetDivider();
        again = RTC_GetCounter();
    } while (cnt != again);

    if (seconds) {
        *seconds = (time_t)((uint32_t)cnt + RTC_EPOCH_OFFSET);
    }
    if (microseconds) {
        /* DIV counts down from PRL to 0 across the second, so the elapsed
         * fraction is (PRL - DIV) / (PRL + 1). In 64 bits because the
         * numerator overflows a 32-bit multiply for every source here. */
        uint32_t prl = s_hz - 1;
        uint32_t elapsed = (div > prl) ? 0 : (prl - div);
        *microseconds = (uint32_t)(((uint64_t)elapsed * 1000000ull) / s_hz);
    }
    return true;
}

bool ch32h4_rtc_set(time_t seconds) {
    if (s_source == CH32H4_RTC_SRC_NONE) {
        return false;
    }
    if (seconds < (time_t)RTC_EPOCH_OFFSET) {
        /* Before 2000, so it cannot be represented. Refusing is better than
         * wrapping into a time 136 years out. */
        return false;
    }
    if (!rtc_wait_write()) {
        return false;
    }
    RTC_SetCounter((uint32_t)((uint32_t)seconds - RTC_EPOCH_OFFSET));
    return rtc_wait_write();
}

bool ch32h4_rtc_is_set(void) {
    if (s_source == CH32H4_RTC_SRC_NONE) {
        return false;
    }
    return RTC_GetCounter() >= RTC_SET_THRESHOLD;
}

/* ---- the C library ------------------------------------------------------ */

/* newlib calls this out of time(), and FatFs' get_fattime() goes through it
 * too, so a file written after an SNTP sync carries the right timestamp
 * without anything else having to know an RTC exists.
 *
 * Weak in newlib and strong here. Returning failure rather than a made-up
 * time when the clock is not running is deliberate: a caller that gets
 * 1970-01-01 can at least tell that it did. */
int _gettimeofday(struct timeval *tv, void *tz) {
    (void)tz;
    if (!tv) {
        return -1;
    }
    time_t sec;
    uint32_t usec;
    if (!ch32h4_rtc_get(&sec, &usec)) {
        tv->tv_sec = 0;
        tv->tv_usec = 0;
        return -1;
    }
    tv->tv_sec = sec;
    tv->tv_usec = (suseconds_t)usec;
    return 0;
}

int settimeofday(const struct timeval *tv, const struct timezone *tz) {
    (void)tz;
    if (!tv) {
        return -1;
    }
    return ch32h4_rtc_set(tv->tv_sec) ? 0 : -1;
}
