/* USBFS device stack bring-up. */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/* Bring up the USB clock, pins and device stack.
 *
 * Returns false and leaves USB down if the part booted on the internal RC --
 * full-speed USB needs a 0.25%-accurate clock and an RC cannot provide it, and
 * a device that half enumerates is worse than one that does not. Also false if
 * the USBHS PLL never locks. Safe to call more than once. */
bool ch32h4_usb_init(void);

/* Whether the stack came up. */
bool ch32h4_usb_active(void);

/* Pump the device stack. Called from loop() by the core; it also runs from the
 * USB interrupt, so a sketch that blocks cannot starve it. */
void ch32h4_usb_task(void);

/* The chip's unique ID is 8 bytes here, despite the SDK's STM32-shaped layout
 * implying 12 -- bytes 8..11 read as erased flash. */
#define CH32H4_UID_BYTES  8

/* The unique ID as hex characters plus a NUL. The buffer must be at least
 * CH32H4_UID_BYTES * 2 + 1 bytes. */
void ch32h4_usb_serial_number(char *buf);

#ifdef __cplusplus
}
#endif
