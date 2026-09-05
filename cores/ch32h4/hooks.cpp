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
#include "ch32h4_fault.h"

#ifdef CH32H4_USB
#include "Adafruit_TinyUSB.h"
#include "ch32h4_usb.h"
#endif

/* Defined by the Ticker library when a sketch links it. */
extern "C" void ch32h4_ticker_update(void) __attribute__((weak));

/* Defined by lwIP_Ethernet. lwIP's timers -- DHCP renewal, TCP
 * retransmission, ARP ageing -- have nothing else driving them under NO_SYS,
 * so a stack that is never pumped appears to work and then quietly stops
 * renewing its lease. */
extern "C" void ch32h4_net_update(void) __attribute__((weak));

extern "C" {

void yield(void) {
#ifdef CH32H4_USB
    /* The USB stack belongs to the V5F. loop1() on the V3F calls delay(),
     * which calls yield(), and letting that reach tud_task() would put two
     * cores inside TinyUSB's event queue at once -- which it has no locking
     * for and would corrupt silently. */
    if (ch32h4_core_num() != 1u) {
        return;
    }

    /* Re-entrancy guard, and it is not optional.
     *
     * Adafruit_USBD_CDC::write(), ::available() and ::operator bool() all call
     * yield() themselves while they wait for the host. yield() here calls
     * TinyUSB_Device_FlushCDC(), which goes back into the CDC object. Without
     * this flag those two call each other until the stack is gone: the
     * observed failure was a trap at mepc=0x200C0002, twenty-two kilobytes
     * below the top of a sixteen-kilobyte stack, having run down through the
     * V3F's stack, .bss and .data into the RAM_LOAD region and executed it.
     *
     * Giving up on re-entry is safe. The outer call is already doing the work,
     * and the USB interrupt runs the device task independently, so nothing is
     * lost -- only repeated. */
    static volatile bool in_yield = false;
    if (in_yield) {
        return;
    }
    in_yield = true;

    /* Weak in Adafruit's API, and only strong once the device stack is linked
     * in, so these are null checks rather than guesses. */
    /* Under the device-stack lock, not bare.
     *
     * TinyUSB_Device_Task() is Adafruit's own one-line wrapper around
     * tud_task(), and TinyUSB_Device_FlushCDC() walks the CDC instances
     * calling tud_cdc_n_write_flush(). Neither takes any lock. The USBFS
     * interrupt runs tud_task() too -- it has to, or a sketch that blocks in
     * loop() lets the event FIFO fill and TU_ASSERT kills the stack -- so
     * calling these bare from here puts two tud_task()s inside one event
     * queue, one of them interrupting the other. That corrupts silently and
     * presents much later as a hang inside an interrupt handler.
     *
     * Losing the round when the interrupt already holds the lock costs
     * nothing: it is doing this work, and the next yield() comes round in
     * microseconds. */
    if (TinyUSB_Device_Task && ch32h4_usb_lock()) {
        TinyUSB_Device_Task();
        if (TinyUSB_Device_FlushCDC) {
            TinyUSB_Device_FlushCDC();
        }
        ch32h4_usb_unlock();
    }

    in_yield = false;
#endif

    /* Software timers, if the sketch linked the Ticker library. Weak, so a
     * sketch that does not use it pays nothing and the symbol resolves to
     * null. */
    if (ch32h4_ticker_update) {
        ch32h4_ticker_update();
    }
    if (ch32h4_net_update) {
        ch32h4_net_update();
    }
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
