/* TinyUSB configuration for the CH32H41x Arduino core.
 *
 * Device only, USBFS, no OS.
 *
 * Adafruit_TinyUSB_Arduino is the USB stack -- the only one. The board option
 * chooses between it and no USB at all.
 *
 * That library ships its own src/tusb_config.h which dispatches on the core's
 * architecture macro and ends in `#error TinyUSB Arduino Library does not
 * support your core yet` for ARDUINO_ARCH_CH32H4. Rather than patch the
 * submodule, the build puts cores/ch32h4 ahead of the library on the include
 * path, so `#include "tusb_config.h"` finds this one.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_TUSB_MCU                OPT_MCU_CH32H417
#define CFG_TUSB_OS                 OPT_OS_NONE

#define CFG_TUD_ENABLED             1
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG              0
#endif

/* Where TinyUSB's transfer buffers go.
 *
 * .usbram is 8 KB of the shared region that nothing else claims, so putting
 * them there is a few kilobytes of DTCM back. That is the whole of the reason:
 * this is a memory budget, not a correctness requirement.
 *
 * It was written here as a correctness requirement -- "the USB controller's
 * bus master cannot see DTCM" -- and that is wrong. It is true of the Ethernet
 * DMA; it was assumed of USB by analogy and never tested. Building with these
 * buffers in DTCM instead gives a device that enumerates, transfers, and
 * passes all of tests/hw/test_usb.py.
 *
 * .usbram is NOLOAD and outside the _sbss.._ebss range the startup code
 * clears; ch32h4_usb_init() zeroes it explicitly, and THAT part is required.
 *
 * THIS FILE HAS A TWIN, libraries/Adafruit_TinyUSB_Arduino/src/arduino/ports/ch32h4/tusb_config_ch32h4.h.
 * The same configuration reached by two include paths -- tusb_option.h
 * includes "tusb_config.h" in quotes and finds src/tusb_config.h next to
 * itself before any -I is consulted, so under arduino-cli the fork's copy
 * wins and under PlatformIO this one does. They must be changed together. */
#define CFG_TUSB_MEM_SECTION        __attribute__((section(".usbram")))
#define CFG_TUSB_MEM_ALIGN          __attribute__((aligned(4)))

#define CFG_TUD_ENDPOINT0_SIZE      64

/* Class drivers.
 *
 * A sketch may ask for HID, MSC, MIDI or a vendor interface at any time and
 * the counts have to be compiled in, so these match what Adafruit's own ch32
 * port uses. Each is overridable from the build, so a sketch that wants none
 * of it pays for none of it. */
#ifndef CFG_TUD_CDC
#define CFG_TUD_CDC                 1
#endif
#ifndef CFG_TUD_MSC
#define CFG_TUD_MSC                 1
#endif
#ifndef CFG_TUD_HID
#define CFG_TUD_HID                 2
#endif
#ifndef CFG_TUD_MIDI
#define CFG_TUD_MIDI                1
#endif
#ifndef CFG_TUD_VENDOR
#define CFG_TUD_VENDOR              1
#endif

#define CFG_TUD_MSC_EP_BUFSIZE      512
#define CFG_TUD_HID_EP_BUFSIZE      64
#define CFG_TUD_MIDI_RX_BUFSIZE     128
#define CFG_TUD_MIDI_TX_BUFSIZE     128
#ifndef CFG_TUD_VENDOR_RX_BUFSIZE
#define CFG_TUD_VENDOR_RX_BUFSIZE   64
#endif
#ifndef CFG_TUD_VENDOR_TX_BUFSIZE
#define CFG_TUD_VENDOR_TX_BUFSIZE   64
#endif

/* 256 each way. The RX buffer absorbs a host burst while a sketch is busy in
 * loop(); the TX buffer lets Serial.print() return promptly rather than
 * blocking on the wire. */
#define CFG_TUD_CDC_RX_BUFSIZE      256
#define CFG_TUD_CDC_TX_BUFSIZE      256
#define CFG_TUD_CDC_EP_BUFSIZE      64

#ifdef __cplusplus
}
#endif
