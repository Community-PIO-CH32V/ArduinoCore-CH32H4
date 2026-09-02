/* The parts of the Arduino API that ArduinoCore-API declares and does not
 * implement.
 *
 * api/Common.h prototypes pulseIn, pulseInLong, shiftIn, shiftOut, random and
 * randomSeed; api/Common.cpp implements map() and makeWord() and nothing else.
 * A core that does not supply the rest links fine until a sketch calls one and
 * then fails on an undefined reference to a function the API documents as
 * standard. Every one of these was missing here, along with dtostrf() and
 * interrupts()/noInterrupts(), which the API does not declare at all but which
 * every other core provides.
 *
 * Linkage matters and is not uniform. pulseIn, pulseInLong, shiftIn and
 * shiftOut are declared in Common.h's C section, so they need C linkage;
 * random and randomSeed are declared only inside its `#ifdef __cplusplus`
 * block, so they need C++ linkage. Getting one wrong leaves exactly the
 * undefined reference this file exists to remove.
 */
#include "Arduino.h"

#include <stdio.h>
#include <string.h>

extern "C" {

/* ---- shift -------------------------------------------------------------- */

/* Bit-banged, and deliberately not timed. Arduino's shiftOut has no defined
 * clock rate -- it runs as fast as digitalWrite allows, which on a 400 MHz
 * part is a great deal faster than on an AVR. A device needing a slower clock
 * wants SPI with a divider, or its own loop. Putting a delay here would make
 * the fast case impossible and still not amount to a specification. */
void shiftOut(pin_size_t dataPin, pin_size_t clockPin, BitOrder bitOrder,
              uint8_t val) {
    for (uint8_t i = 0; i < 8; i++) {
        if (bitOrder == LSBFIRST) {
            digitalWrite(dataPin, (val & 1) ? HIGH : LOW);
            val = (uint8_t)(val >> 1);
        } else {
            digitalWrite(dataPin, (val & 0x80) ? HIGH : LOW);
            val = (uint8_t)(val << 1);
        }
        digitalWrite(clockPin, HIGH);
        digitalWrite(clockPin, LOW);
    }
}

uint8_t shiftIn(pin_size_t dataPin, pin_size_t clockPin, BitOrder bitOrder) {
    uint8_t value = 0;
    for (uint8_t i = 0; i < 8; i++) {
        digitalWrite(clockPin, HIGH);
        if (digitalRead(dataPin) == HIGH) {
            value |= (uint8_t)(1u << ((bitOrder == LSBFIRST) ? i : (7 - i)));
        }
        digitalWrite(clockPin, LOW);
    }
    return value;
}

/* ---- pulseIn ------------------------------------------------------------ */

/* Both variants measure with micros().
 *
 * On AVR, pulseIn() counts a hand-tuned busy loop while pulseInLong() uses
 * micros(), because there the loop resolves better than a 4 us timer tick.
 * Here micros() comes off a 400 MHz core and beats anything that could be
 * written as a loop, so the two are the same function -- which is what the
 * SAMD and mbed cores also do.
 *
 * Returns 0 on timeout, as Arduino specifies. A 0 is therefore ambiguous with
 * a pulse under a microsecond; that ambiguity is in the API, not here.
 */
static unsigned long pulse_in(pin_size_t pin, uint8_t state,
                              unsigned long timeout) {
    const unsigned long start = micros();
    const PinStatus want = state ? HIGH : LOW;

    /* Three waits sharing ONE timeout, which is what Arduino does: the
     * argument bounds the whole call rather than each phase. Wait out a pulse
     * already in progress, then for the leading edge, then time the pulse. */
    while (digitalRead(pin) == want) {
        if (micros() - start > timeout) {
            return 0;
        }
    }
    while (digitalRead(pin) != want) {
        if (micros() - start > timeout) {
            return 0;
        }
    }

    const unsigned long pulse_start = micros();
    while (digitalRead(pin) == want) {
        if (micros() - start > timeout) {
            return 0;
        }
    }
    return micros() - pulse_start;
}

unsigned long pulseIn(pin_size_t pin, uint8_t state, unsigned long timeout) {
    return pulse_in(pin, state, timeout);
}

unsigned long pulseInLong(pin_size_t pin, uint8_t state,
                          unsigned long timeout) {
    return pulse_in(pin, state, timeout);
}

/* ---- dtostrf ------------------------------------------------------------ */

/* avr-libc's, which the API does not declare but a great many sketches and
 * libraries call. Width is signed: negative left-justifies.
 *
 * Implemented over snprintf rather than by hand. The one thing to know is that
 * it writes into a caller-supplied buffer with no length -- that is avr-libc's
 * signature and it cannot be fixed here, so the buffer must be big enough for
 * the width requested plus a terminator. */
char *dtostrf(double val, signed char width, unsigned char prec, char *sout) {
    char fmt[24];
    snprintf(fmt, sizeof(fmt), "%%%d.%uf", (int)width, (unsigned)prec);
    sprintf(sout, fmt, val);
    return sout;
}

}   /* extern "C" */

/* ---- random ------------------------------------------------------------- */

/* Deliberately NOT the hardware TRNG.
 *
 * Arduino's random() is specified to be repeatable from a given seed -- that
 * is the entire purpose of randomSeed(), and sketches rely on it for
 * reproducible behaviour. Wiring this to the TRNG would make randomSeed()
 * meaningless, and would invite the opposite mistake too: this is not a
 * cryptographic generator and must not be taken for one. A sketch wanting real
 * entropy calls the TRNG directly, as mbedtls already does through
 * ch32h4_rng.
 *
 * Its own state rather than newlib's random(): only one function of a given
 * name may have C linkage, and newlib's `long random(void)` already claims it,
 * so calling through to it from a C++ `random(long)` overload is a trap that
 * works until something includes stdlib.h in a different order.
 *
 * xorshift32. Not the best generator known, but it is short, has a full
 * 2^32-1 period, and passes the things a sketch cares about.
 */
static uint32_t s_random_state = 1;

static uint32_t random_next(void) {
    uint32_t x = s_random_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    s_random_state = x;
    return x;
}

void randomSeed(unsigned long seed) {
    /* Zero is the one seed that must be refused: it is a fixed point of
     * xorshift, so seeding with it returns zero forever. A sketch seeding from
     * an unconnected analogRead() hits this. Arduino's own implementation
     * makes the same exception. */
    if (seed != 0) {
        s_random_state = (uint32_t)seed;
    }
}

long random(long howbig) {
    if (howbig <= 0) {
        /* Arduino leaves a negative bound undefined; 0 beats a modulus whose
         * sign is implementation-defined. */
        return 0;
    }
    return (long)(random_next() % (uint32_t)howbig);
}

long random(long howsmall, long howbig) {
    if (howsmall >= howbig) {
        return howsmall;
    }
    return random(howbig - howsmall) + howsmall;
}
