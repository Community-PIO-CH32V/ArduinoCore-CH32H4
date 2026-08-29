/* TinyUSB configuration for the CH32H41x Arduino core.
 *
 * Device only, USBFS, no OS. The controller is the newer USBFS IP; see
 * tusb_mcu.h for what OPT_MCU_CH32H417 selects.
 */
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#define CFG_TUSB_MCU                OPT_MCU_CH32H417
#define CFG_TUSB_OS                 OPT_OS_NONE
#define CFG_TUSB_RHPORT0_MODE       (OPT_MODE_DEVICE | OPT_MODE_FULL_SPEED)

#ifndef CFG_TUSB_DEBUG
#define CFG_TUSB_DEBUG              0
#endif

/* Every TinyUSB buffer must be reachable by the USB controller's own bus
 * master, which cannot see DTCM. The linker script puts TinyUSB's .bss in
 * USB_RAM, in the shared region, and ch32h4_usb_init() zeroes it. */
#define CFG_TUSB_MEM_SECTION
#define CFG_TUSB_MEM_ALIGN          __attribute__((aligned(4)))

#define CFG_TUD_ENDPOINT0_SIZE      64

/* Class drivers. CDC is the one the core itself uses for Serial; the rest are
 * off here and enabled by Adafruit_TinyUSB when a sketch asks for them. */
#ifndef CFG_TUD_CDC
#define CFG_TUD_CDC                 1
#endif
#ifndef CFG_TUD_MSC
#define CFG_TUD_MSC                 0
#endif
#ifndef CFG_TUD_HID
#define CFG_TUD_HID                 0
#endif
#ifndef CFG_TUD_MIDI
#define CFG_TUD_MIDI                0
#endif
#ifndef CFG_TUD_VENDOR
#define CFG_TUD_VENDOR              0
#endif

/* 256 each way. The RX buffer is what absorbs a host burst while a sketch is
 * busy in loop(); the TX buffer is what lets Serial.print() return promptly
 * instead of blocking on the wire. */
#define CFG_TUD_CDC_RX_BUFSIZE      256
#define CFG_TUD_CDC_TX_BUFSIZE      256
#define CFG_TUD_CDC_EP_BUFSIZE      64

#ifdef __cplusplus
}
#endif
