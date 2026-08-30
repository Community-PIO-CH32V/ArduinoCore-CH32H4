/* yield(), and the core's USB entry points.
 *
 * yield() is what Arduino guarantees will run whenever a sketch is waiting:
 * inside delay(), inside a blocking Stream read, inside Adafruit's CDC write
 * when the FIFO is full. On a USB core it is therefore where the device task
 * belongs -- without it, a sketch that blocks anywhere stops servicing USB and
 * the host sees the port die.
 *
 * It is not the only place the stack runs. ch32h4_usb.c also calls it from the
 * USBFS interrupt, so even a sketch that never yields cannot starve TinyUSB
 * into a full event FIFO, which is a TU_ASSERT and on this SDK an ebreak into
 * a handler that spins forever.
 */
#include "Arduino.h"

#ifdef CH32H4_USB
#include "Adafruit_TinyUSB.h"
#endif

extern "C" {

void yield(void) {
#ifdef CH32H4_USB
    /* Weak in Adafruit's API, and only strong once the device stack is linked
     * in, so this is a null check rather than a guess. */
    if (TinyUSB_Device_Task) {
        TinyUSB_Device_Task();
    }
    if (TinyUSB_Device_FlushCDC) {
        TinyUSB_Device_FlushCDC();
    }
#endif
}

/* Called from ch32h4_v5f_main(), which is C.
 *
 * Goes through Adafruit's TinyUSB_Device_Init() rather than straight to
 * ch32h4_usb_init(), because that is what constructs Adafruit_USBD_Device and
 * lets it compose the descriptor from whatever interfaces the sketch's
 * globals registered -- CDC always, plus HID, MSC or MIDI if the sketch
 * instantiated them. Calling our init directly would bring the controller up
 * with no descriptor owner.
 *
 * Returns whether USB actually came up; it refuses to start on the internal
 * RC, since full-speed USB cannot meet spec from an RC oscillator. */
bool ch32h4_usb_begin(void) {
#ifdef CH32H4_USB
    if (TinyUSB_Device_Init) {
        TinyUSB_Device_Init(0);
    }
    return ch32h4_usb_active();
#else
    return false;
#endif
}

}  /* extern "C" */
