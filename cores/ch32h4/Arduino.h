/* The umbrella header every sketch gets.
 *
 * Note that ArduinoCore-API is reached as "api/ArduinoAPI.h" and that
 * cores/ch32h4/api is NOT on the include path. On a case-insensitive
 * filesystem, putting it there makes <string.h> resolve to the API's own
 * String.h and every use of strlen, memcpy and memset inside the API fails to
 * compile. arduino-pico keeps the directory off the path for the same reason.
 */
#pragma once

#include <stdint.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

#include "api/ArduinoAPI.h"

#include "ch32h417.h"

#include "ch32h4_clock.h"
#include "ch32h4_console.h"
#include "ch32h4_gpio.h"
#include "ch32h4_itcm.h"
#include "ch32h4_pinmap.h"
#include "ch32h4_timer.h"
#include "ch32h4_rcc.h"
#include "ch32h4_xcore.h"

#include "pins_arduino.h"

#ifdef __cplusplus

using namespace arduino;

#include "HardwareSerial.h"

/* setup1() and loop1() run on the V3F and arrive in M4. They are declared here
 * now so a sketch written against arduino-pico's model still compiles; until
 * M4 they are simply never called, and the core says so at boot rather than
 * leaving the sketch to wonder. */
extern void setup1() __attribute__((weak));
extern void loop1() __attribute__((weak));

#endif /* __cplusplus */

#ifdef __cplusplus
extern "C" {
#endif

/* Arduino's analogReference() takes a mode this part does not have -- VDDA is
 * the only reference. Provided so sketches link; it does nothing.
 *
 * The real check on "is VDDA actually 3.3 V" is ADC1_IN17, the internal
 * 1.20 V reference, which ch32h4_vdda_volts() reads. */
void analogReference(uint8_t mode);

void analogReadResolution(int bits);
void analogWriteResolution(int bits);

/* PWM frequency, in Hz. Applies to every timer analogWrite() subsequently
 * brings up; the period register is shared by a timer's four channels, so
 * pins that land on one timer share a frequency. Default 1 kHz. */
void analogWriteFrequency(uint32_t hz);

/* Stop driving a PWM pin, and release its timer once no channels remain, so
 * Servo or tone() can claim it. Arduino has no such call, but anything sharing
 * these twelve timers needs one. */
void analogWriteStop(pin_size_t pin);

/* Measures VDDA against the internal reference on ADC1_IN17, nominally
 * 1.20 V. Returns volts, or 0.0f if the conversion failed. */
float ch32h4_vdda_volts(void);

/* Free heap across both regions -- DTCM first, then the shared half. */
size_t ch32h4_heap_free(void);

#ifdef __cplusplus
}
#endif
