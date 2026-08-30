/* The port layer Adafruit_TinyUSB_Arduino asks a core to supply.
 *
 * That library ships ports for samd, nrf, rp2040, esp32, stm32 and ch32 -- but
 * its ch32 port is guarded on ARDUINO_ARCH_CH32 / CH32V20x / CH32V30x and
 * targets the older USB IPs, none of which is this part. Rather than patch the
 * submodule, the core supplies the three functions here and the USBFS
 * interrupt in ch32h4_usb.c.
 *
 * Three functions is the whole contract; see
 * libraries/Adafruit_TinyUSB_Arduino/src/arduino/Adafruit_TinyUSB_API.h.
 */
#ifdef CH32H4_USB

#include "Arduino.h"
#include "ch32h4_usb.h"
#include "tusb.h"

/* NOT inside extern "C": Adafruit_TinyUSB_API.h declares these with ordinary
 * C++ linkage, and wrapping the definitions gives "conflicting declaration
 * ... with 'C' linkage". Including that header (via Arduino.h) is what makes
 * the linkage match. */

/* Called by Adafruit_USBD_Device::begin(). The real work -- the USBHS PLL at
 * 480 MHz divided by 10 for USBFS's mandatory 48, the pins, the OTG_FS clock,
 * zeroing TinyUSB's relocated .bss -- is in ch32h4_usb_init(), which is shared
 * with the core's own startup and is safe to call more than once.
 *
 * rhport is ignored: this part has one full-speed device controller. */
void TinyUSB_Port_InitDevice(uint8_t rhport) {
    (void)rhport;
    ch32h4_usb_init();
}

/* No DFU bootloader is reachable from software on this part. WCH's factory
 * bootloader is entered by holding BOOT0 at reset, which is a physical action,
 * so there is nothing honest to do here -- and pretending otherwise by jumping
 * at a guessed address would hang the board rather than reset it. */
void TinyUSB_Port_EnterDFU(void) {
}

/* The chip's unique ID, as raw bytes. Adafruit formats it into the string
 * descriptor itself, so this returns the byte count rather than a string.
 *
 * Eight bytes on this part, not the twelve an STM32-shaped layout implies --
 * bytes 8..11 read as erased flash, so a 12-byte read gives every board a
 * serial ending in the same 39E339E3. */
uint8_t TinyUSB_Port_GetSerialNumber(uint8_t serial_id[16]) {
    const uint8_t *id = (const uint8_t *)0x1FFFF7E8;
    for (uint8_t i = 0; i < CH32H4_UID_BYTES; i++) {
        serial_id[i] = id[i];
    }
    return CH32H4_UID_BYTES;
}

#endif /* CH32H4_USB */
