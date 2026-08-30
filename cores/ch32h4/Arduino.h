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

/* `Serial`.
 *
 * With USB in the build this is Adafruit's CDC device. Adafruit_USBD_CDC.h
 * does `#define SerialTinyUSB Serial` itself under USE_TINYUSB, so the core
 * must NOT define `Serial` as well -- doing so gave a conflicting declaration
 * of Adafruit_USBD_CDC. Including the library here is what makes `Serial` work
 * in a sketch that never mentions TinyUSB, which is the whole point of USB
 * being the default.
 *
 * Set board_build.serial = uart to put `Serial` back on USART1. That is worth
 * doing while debugging anything that can fault before USB has enumerated,
 * since a CDC-only board cannot report such a fault at all.
 *
 * `Serial1` is USART1 on PA9/PA10 either way. */
#if defined(CH32H4_USB) && defined(CH32H4_SERIAL_IS_USB)
#include "ch32h4_usb.h"
#include "Adafruit_TinyUSB.h"
#else
#ifdef CH32H4_USB
#include "ch32h4_usb.h"
#include "Adafruit_TinyUSB.h"
#endif
#define Serial Serial1
#endif

/* setup1() and loop1() run on the V3F.
 *
 * Define either and the V3F runs it after the V5F has finished constructing
 * the sketch's globals; define neither and the V3F sleeps, which is what a
 * single-core sketch wants and costs it nothing. */
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

/* Runs whenever a sketch is waiting: inside delay(), inside a blocking Stream
 * read, inside a USB write with a full FIFO. This is where the USB device task
 * runs, so anything that blocks must reach it. */
void yield(void);

#ifdef __cplusplus
}
#endif

#ifdef __cplusplus
/* Last, because it uses the declarations above. */
#include "CH32H4Core.h"
#endif
