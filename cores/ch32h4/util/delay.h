/* AVR compatibility: <util/delay.h>.
 *
 * Plenty of libraries written in the AVR era include this unconditionally, or
 * behind a guard that lists every architecture its author had heard of --
 * Adafruit_SSD1306's is `#if !defined(__ARM_ARCH) && !defined(ESP8266) && ...`,
 * and a RISC-V core that is not on the list falls through to it.
 *
 * On AVR these take a compile-time constant and become a calibrated busy loop.
 * Here they are just the Arduino delay functions, which is what every non-AVR
 * core does and what the callers actually mean.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

void delay(unsigned long ms);
void delayMicroseconds(unsigned int us);

#ifdef __cplusplus
}
#endif

/* Doubles, because the AVR originals take them and callers pass literals like
 * _delay_ms(1.5). */
static inline void _delay_ms(double ms) {
    if (ms > 0) {
        delay((unsigned long)ms);
    }
}

static inline void _delay_us(double us) {
    if (us > 0) {
        delayMicroseconds((unsigned int)us);
    }
}
