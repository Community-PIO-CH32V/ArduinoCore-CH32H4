/* The real-time clock.
 *
 * Ported from the MicroPython port for this silicon, findings and all.
 *
 * The peripheral is the STM32F1 one: a 32-bit second counter fed through a
 * 20-bit prescaler, living in the backup domain so it keeps running across a
 * reset. There is no BCD calendar in hardware; everything above "seconds" is
 * the C library's job, which is why this integrates with gettimeofday() rather
 * than inventing its own struct.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>
#include <sys/time.h>
#include <time.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    CH32H4_RTC_SRC_NONE = 0,

    /* 32768 Hz from the crystal at PC14/PC15. Divides to exactly one second,
     * and is the only source inside the backup domain -- so it is the only one
     * that keeps time when VDD33 goes away and VBAT does not. This board has
     * the crystal fitted (Y1, 12 pF loading capacitors). */
    CH32H4_RTC_SRC_LSE = 1,

    /* The internal RC. Needs no crystal and cannot be stopped, but it is only
     * specified to 25-60 kHz -- it measured 41.3 kHz on this board -- so the
     * nominal 40000 divisor can be several percent out. That is minutes a day.
     * Fine for elapsed time, not for a clock. */
    CH32H4_RTC_SRC_LSI = 2,

    /* The 25 MHz crystal divided by 512, so 48828.125 Hz, which is not a whole
     * number of ticks per second. The nearest divisor gains about 2.6 ppm, a
     * fifth of a second a day. Accurate, but it stops with VDD33, so it is for
     * boards with no 32 kHz crystal that still want a good clock while
     * powered. */
    CH32H4_RTC_SRC_HSE = 3,
} ch32h4_rtc_src_t;

/* Start the clock, or adopt one already running.
 *
 * RTCSEL cannot be changed once written -- only a backup-domain reset reopens
 * it, and that clears the counter. So asking for a source that is ALREADY
 * running keeps the time; asking for a different one restarts the clock from
 * the epoch. After a warm reset the normal case is the former.
 *
 * Returns false if the oscillator never came ready, which for the LSE means
 * no crystal.
 */
bool ch32h4_rtc_begin(ch32h4_rtc_src_t src);

/* Which source is actually running, read from RCC rather than remembered: the
 * backup domain survives a reset, so after a warm boot the clock is already
 * going and the firmware that started it is gone. */
ch32h4_rtc_src_t ch32h4_rtc_source(void);
uint32_t ch32h4_rtc_hz(void);
bool ch32h4_rtc_running(void);

/* Has the clock been set to a real time, as opposed to merely running?
 *
 * A counter ticking up from the epoch is not the same as knowing what time it
 * is, and code that validates a certificate or stamps a file needs to tell
 * them apart. Derived from the counter rather than stored: this part has no
 * backup data registers, and a clock nobody has set reads as 2000-01-01. */
bool ch32h4_rtc_is_set(void);

/* Seconds since the Unix epoch, and the fraction of the current second.
 * Returns false if the clock is not running. */
bool ch32h4_rtc_get(time_t *seconds, uint32_t *microseconds);
bool ch32h4_rtc_set(time_t seconds);

/* Counter ticks, for elapsed-time use that does not care about the wall clock.
 * Unsigned and free-running, so differences are correct across a wrap. */
uint32_t ch32h4_rtc_ticks(void);

#ifdef __cplusplus
}
#endif
