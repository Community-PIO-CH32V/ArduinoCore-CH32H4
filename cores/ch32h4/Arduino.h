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
/* Asking for a USB console without a USB stack used to fall through to the
 * #else below and quietly hand back USART1 -- the board came up, printed
 * nothing to the port the user was watching, and nothing said why. The two
 * are separate menu entries now, which makes the combination easy to select
 * by accident, so it is refused. */
#if defined(CH32H4_SERIAL_IS_USB) && !defined(CH32H4_USB)
#error "Serial is set to USB CDC but the USB stack is None. Pick a USB stack \
(Tools > USB stack > Adafruit TinyUSB), or set Serial to USART1."
#endif

/* The stack first, unconditionally when it is compiled in, so that whatever
 * Adafruit declares is parsed before `Serial` means anything here. */
#ifdef CH32H4_USB
#include "ch32h4_usb.h"
#include "Adafruit_TinyUSB.h"
#endif

/* Then name `Serial`, and name it in exactly one place.
 *
 * Adafruit's header would alias its CDC object to `Serial` itself, which is
 * right for a core where USB implies a USB console. Here they are separate
 * menu entries, so the fork skips that alias for ARDUINO_ARCH_CH32H4 and the
 * choice is made here instead. Doing it the other way round does not merely
 * look untidy: Adafruit_TinyUSB.h includes this file, so with the alias live
 * the macro below rewrites its own declaration into
 * `Adafruit_USBD_CDC Serial1` and collides with the UART. */
#ifdef CH32H4_SERIAL_IS_USB
#define Serial SerialTinyUSB
#else
#define Serial Serial1
#endif

/* setup1() and loop1() run on the V3F.
 *
 * Define either and the V3F runs it after the V5F has finished constructing
 * the sketch's globals; define neither and the V3F sleeps, which is what a
 * single-core sketch wants and costs it nothing. */
/* extern "C", like setup() and loop() in ArduinoCore-API's Common.h.
 *
 * Without it a sketch's `void setup1()` is a C++ symbol -- _Z6setup1v -- while
 * main_v3f.c, which is C, looks for the unmangled name. The weak reference
 * then stays null, the V3F decides the sketch has no second-core code and goes
 * to sleep, and the sketch's loop1() is silently never called. Nothing
 * anywhere reports it. */
extern "C" {
extern void setup1() __attribute__((weak));
extern void loop1() __attribute__((weak));
}

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

/* The ADC is 12-bit. analogRead() defaults to Arduino's 10 and shifts, so
 * analogReadResolution(12) is what a sketch calls to get the full range;
 * above 12 the added low bits are zeros. Applies to analogRead() only --
 * ADCInput always delivers raw 12-bit samples. */
/* Global interrupt enable/disable.
 *
 * The Arduino API does not declare these -- they are an AVR inheritance that
 * every core provides anyway, and a great many libraries call them. On RISC-V
 * the bit is MIE, mstatus[3].
 *
 * noInterrupts() does NOT nest. Two nested pairs leave interrupts on after the
 * inner one, which is the same trap AVR's cli/sei has and the same one every
 * other core reproduces; code that needs nesting must save mstatus itself. */
static inline void interrupts(void) {
    __asm volatile("csrsi mstatus, 8" ::: "memory");
}

static inline void noInterrupts(void) {
    __asm volatile("csrci mstatus, 8" ::: "memory");
}

/* avr-libc's float formatter. Not in the Arduino API, but too widely used to
 * leave out. Width is signed -- negative left-justifies -- and there is no
 * length argument, so `sout` must be large enough for the width plus a
 * terminator. */
char *dtostrf(double val, signed char width, unsigned char prec, char *sout);

void analogReadResolution(int bits);
int  analogReadResolutionBits(void);

void analogWriteResolution(int bits);

/* PWM frequency, in Hz. Applies to every timer analogWrite() subsequently
 * brings up; the period register is shared by a timer's four channels, so
 * pins that land on one timer share a frequency. Default 1 kHz. */
void analogWriteFrequency(uint32_t hz);

/* Stop driving a PWM pin, and release its timer once no channels remain, so
 * Servo or tone() can claim it. Arduino has no such call, but anything sharing
 * these twelve timers needs one. On a DAC pin it switches the DAC channel off
 * instead, which is the matching thing to undo. */
void analogWriteStop(pin_size_t pin);


/* ---- the two 12-bit DACs, on PA4 (DAC1) and PA5 (DAC2) ------------------
 *
 * Written through analogWrite(), the way STM32duino and the SAMD core do it:
 * a DAC-capable pin goes to the DAC and every other pin to a timer, so there
 * is no dacWrite() to learn and a sketch from either of those cores works
 * unchanged. analogWriteResolution() scales the argument.
 *
 *     analogWriteResolution(12);
 *     analogWrite(DAC1, 2048);       // mid-rail
 *
 * The calls below are for what analogWrite() cannot express. */

/* True for PA4 and PA5. These pins are fixed -- no mux reaches either DAC
 * anywhere else -- and analogWrite() sends them to the DAC, so neither can
 * produce PWM through analogWrite() even though both have timer channels. */
bool ch32h4_pin_has_dac(pin_size_t pin);

/* 1 or 2 for the DAC pins, 0 for anything else. */
uint8_t ch32h4_dac_channel(pin_size_t pin);

/* Raw 12-bit write, ignoring analogWriteResolution(). Returns false for a pin
 * with no DAC. */
bool ch32h4_dac_write(pin_size_t pin, uint16_t value12);

/* The code currently held, or 0 if the channel was never started. */
uint16_t ch32h4_dac_read(pin_size_t pin);

bool ch32h4_dac_is_started(pin_size_t pin);
void ch32h4_dac_stop(pin_size_t pin);

/* The output buffer, on by default.
 *
 * Buffered, the DAC can drive a real load; unbuffered it is a high-impedance
 * node that any load pulls off value.
 *
 * Unlike classic STM32, the buffer here costs no useful range -- measured
 * against the ADC, a buffered channel reads 1 at code 0 and 4090 at code
 * 4095, against 0 and 4089 unbuffered. This is for drive strength, not for
 * range, and the default is right for almost everything.
 *
 * Takes effect on the next write. */
bool ch32h4_dac_output_buffer(pin_size_t pin, bool enable);


/* Measures VDDA against the internal reference on ADC1_IN17, nominally
 * 1.20 V. Returns volts, or 0.0f if the conversion failed. */
float ch32h4_vdda_volts(void);

/* Die temperature in degrees Celsius, from ADC1_IN16 and the factory
 * calibration. Returns 0.0f if the conversion failed or the part has no
 * calibration programmed.
 *
 * This is the DIE, several degrees above ambient on a part clocked at
 * 400 MHz, and WCH specifies the sensor for measuring changes rather than
 * absolute temperature. Treat a single reading as approximate. */
float analogReadTemp(void);

/* Which ADC channel a pin selects, or 0xFF for one with no ADC input. Accepts
 * the internal pseudo-pins ATEMP and AVREF as well as real pins, and is the
 * single place that mapping is decided -- analogRead() and ADCInput both go
 * through it so they cannot disagree. */
uint8_t ch32h4_adc_channel(pin_size_t pin);

/* True for ATEMP and AVREF, which have no pad: nothing may index g_pins with
 * them, and nothing may try to configure them as GPIO. */
bool ch32h4_adc_is_internal(pin_size_t pin);

/* Put a real pin into analog mode. Does nothing for the internal channels. */
void ch32h4_adc_prepare_pin(pin_size_t pin);

/* There is one ADC, and a timer-paced capture owns its regular sequence for
 * as long as it runs. ADCInput claims it across begin()/end(); analogRead()
 * returns 0 while it is claimed rather than starting a conversion that would
 * both answer with the wrong channel and drop a scan from the capture. */
void ch32h4_adc_claim(void);
void ch32h4_adc_release(void);
int  ch32h4_adc_is_capturing(void);

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
