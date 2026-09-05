/* itoa/ltoa/utoa/ultoa -- the four the Arduino API requires a core to supply.
 *
 * api/itoa.h declares all four and says "the core should supply an
 * implementation" if the standard library does not. This core never did, and
 * newlib only has itoa: the other three are avr-libc extensions. So
 * String(millis()) and String(micros()) -- String from any long or unsigned --
 * failed at LINK time, with an "undefined reference to `ultoa'" attributed by
 * LTO to whatever function it had been inlined into, which is a long way from
 * the sketch line that caused it. ArduinoOTA's nonce is String(micros()), which
 * is how it finally turned up.
 *
 * WEAK, so that a newlib that does provide one wins and there is no duplicate
 * symbol either way.
 *
 * Radix 2 through 36, like avr-libc; a signed conversion in any other radix
 * than 10 formats the bit pattern rather than a minus sign, which is also what
 * avr-libc does and what sketches printing hex expect.
 */
#include "api/itoa.h"

static char *utoa_common(unsigned long value, char *string, int radix) {
    char buf[8 * sizeof(unsigned long) + 1];
    char *p = buf + sizeof(buf) - 1;

    *p = '\0';
    do {
        const unsigned long digit = value % (unsigned long)radix;
        *--p = (char)(digit < 10u ? '0' + digit : 'a' + digit - 10u);
        value /= (unsigned long)radix;
    } while (value);

    char *out = string;
    while ((*out++ = *p++) != '\0') {
    }
    return string;
}

static char *ltoa_common(long value, char *string, int radix) {
    if (radix < 2 || radix > 36) {
        *string = '\0';
        return string;
    }
    /* Only base 10 is signed. Everything else formats the bit pattern, which
     * is what avr-libc does and what a sketch printing hex is asking for. */
    if (radix == 10 && value < 0) {
        *string = '-';
        /* Negated as unsigned: -LONG_MIN does not fit in a long. */
        utoa_common(-(unsigned long)value, string + 1, radix);
        return string;
    }
    return utoa_common((unsigned long)value, string, radix);
}

__attribute__((weak)) char *itoa(int value, char *string, int radix) {
    return ltoa_common(value, string, radix);
}

__attribute__((weak)) char *ltoa(long value, char *string, int radix) {
    return ltoa_common(value, string, radix);
}

__attribute__((weak)) char *utoa(unsigned value, char *string, int radix) {
    if (radix < 2 || radix > 36) {
        *string = '\0';
        return string;
    }
    return utoa_common(value, string, radix);
}

__attribute__((weak)) char *ultoa(unsigned long value, char *string, int radix) {
    if (radix < 2 || radix > 36) {
        *string = '\0';
        return string;
    }
    return utoa_common(value, string, radix);
}
