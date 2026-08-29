/* USBFS device support.
 *
 * The controller is on PA11 (OTG_DM) / PA12 (OTG_DP). On this package PA9 and
 * PA10 are OTG_VBUS and OTG_ID, and they carry the UART console, so neither
 * VBUS sensing nor the ID pin is enabled -- a device-only build needs neither.
 *
 * Most of what is delicate here is the clock. Getting it wrong produces a
 * device that never enumerates, with no error anywhere.
 */
#include <string.h>

#include "ch32h417.h"
#include "ch32h4_clock.h"
#include "ch32h4_irq.h"
#include "ch32h4_usb.h"
#include "tusb.h"

/* Bounds of the .usbram section, from the linker script. */
extern uint8_t _susbram[];
extern uint8_t _eusbram[];

static bool s_usb_up = false;

bool ch32h4_usb_active(void) {
    return s_usb_up;
}

bool ch32h4_usb_init(void) {
    if (s_usb_up) {
        return true;
    }

    /* TinyUSB's .bss lives in USB_RAM so the controller's bus master can reach
     * it, which puts it outside the range the startup code clears. Zero it
     * here, before any TinyUSB code runs. */
    memset(_susbram, 0, (size_t)(_eusbram - _susbram));

    /* USB cannot meet spec from the internal RC.
     *
     * Full-speed USB needs a 0.25%-accurate clock and an on-chip RC is roughly
     * an order of magnitude worse. The failure is not a clean one: the SIE
     * still detects bus reset and suspend, which are DC conditions needing no
     * clock, so the device looks alive and simply never decodes a packet.
     * Refuse rather than enumerate something that half works. */
    if (ch32h4_clock_source() != CH32H4_CLOCK_SRC_HSE) {
        return false;
    }

    /* USBFS needs exactly 48 MHz, and the system PLL cannot produce it: SYSCLK
     * is 400 MHz and the USBFS dividers are 1,2,3,4,5,6,8,10 plus half steps,
     * so 400/8.33 is not reachable. The USBHS PLL runs at 480 MHz and 480/10 is
     * exactly 48, so that is the source.
     *
     * Do NOT call RCC_HSEConfig() here to "make sure" the crystal is on. It
     * clears HSEON before setting it, and with the whole clock tree derived
     * from HSE that momentarily removes the reference from under the running
     * system. The USBHS PLL does not recover and never reports lock. */
    RCC_USBHSPLLCLKConfig(RCC_USBHSPLLSource_HSE);
    RCC_USBHSPLLReferConfig(RCC_USBHSPLLRefer_25M);
    RCC_USBHS_PLLCmd(ENABLE);

    /* Wait for lock rather than spinning a fixed count: the USBFS clock mux
     * below does not latch while its source PLL is stopped. */
    bool locked = false;
    for (volatile uint32_t i = 0; i < 2000000u; i++) {
        if (RCC->CTLR & RCC_USBHS_PLLRDY) {
            locked = true;
            break;
        }
    }
    if (!locked) {
        return false;
    }

    RCC_USBFSCLKConfig(RCC_USBFSCLKSource_USBHSPLL);
    RCC_USBFS48ClockSourceDivConfig(RCC_USBFS_Div10);

    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOA | RCC_HB2Periph_AFIO, ENABLE);
    (void)RCC->HB2PCENR;

    /* PA11/PA12 carry the differential pair. This part has an F4-style AF mux,
     * so GPIO_Mode_AF_PP is normally not enough on its own -- but the USB pads
     * are driven by the controller directly rather than through a numbered
     * alternate function, so there is no GPIO_PinAFConfig to do here. */
    GPIO_InitTypeDef gpio = {0};
    gpio.GPIO_Pin = GPIO_Pin_11 | GPIO_Pin_12;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_Very_High;
    GPIO_Init(GPIOA, &gpio);

    /* OTG_FS is on HB. */
    RCC_HBPeriphClockCmd(RCC_HBPeriph_OTG_FS, ENABLE);
    (void)RCC->HBPCENR;

    tusb_init(0);
    NVIC_EnableIRQ(USBFS_IRQn);

    s_usb_up = true;
    return true;
}

/* tud_task() is not reentrant: it drains an event queue and dispatches class
 * callbacks, so a call from the interrupt landing on top of one from loop()
 * would corrupt that state. Whichever arrives second gives up -- safely,
 * because the events it would have handled stay queued for the call already
 * running, or the next one. */
static volatile bool s_in_task;

void ch32h4_usb_task(void) {
    if (!s_usb_up || s_in_task) {
        return;
    }
    s_in_task = true;
    tud_task();
    s_in_task = false;
}

/* The device stack runs from the interrupt as well as from loop().
 *
 * loop() alone is not enough. Any sketch that blocks in C -- a long delay(), a
 * busy I2C transfer, a tight compute loop -- holds the main loop long enough
 * for TinyUSB's event FIFO to fill, and a full FIFO is a TU_ASSERT, which on
 * this SDK is an ebreak into a handler that spins forever with the board dead
 * to USB and unrecoverable without a reset. The MicroPython port paid for this
 * three times before doing it this way, and ports/mimxrt does the same.
 *
 * Calling the task here means no amount of rudeness in a sketch can starve the
 * USB stack: the worst that happens is that the work is late, not fatal. */
void CH32H4_IRQ_HANDLER(USBFS_IRQHandler);
void USBFS_IRQHandler(void) {
    tud_int_handler(0);
    if (!s_in_task) {
        s_in_task = true;
        tud_task();
        s_in_task = false;
    }
}

/* The chip's 96-bit unique ID, as a hex string.
 *
 * The caller walks the buffer looking for a NUL, so terminating it is not
 * optional: without it the serial descriptor picks up whatever follows on the
 * stack and is malformed in a way that depends on the caller's stack contents.
 * That fails enumeration AFTER the device descriptor has been read
 * successfully, and only in some builds. */
void ch32h4_usb_serial_number(char *buf) {
    const uint8_t *id = (const uint8_t *)0x1FFFF7E8;
    static const char hex[] = "0123456789ABCDEF";
    for (int i = 0; i < 12; i++) {
        buf[i * 2] = hex[id[i] >> 4];
        buf[i * 2 + 1] = hex[id[i] & 0xF];
    }
    buf[24] = '\0';
}
