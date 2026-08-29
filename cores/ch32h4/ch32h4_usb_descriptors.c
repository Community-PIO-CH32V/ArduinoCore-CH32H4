/* The core's default USB descriptors: a single CDC ACM interface.
 *
 * These are weak, so Adafruit_TinyUSB replaces the whole set when a sketch
 * pulls it in and wants a composite device. What is here is only what the core
 * needs to give a sketch a working `Serial` out of the box.
 */
#include "ch32h4_usb.h"
#include "tusb.h"

/* pid.codes 0x1209:0x0001, the generic prototype pair.
 *
 * NOT 1A86:8010 -- those are the WCH-LinkE's own identifiers, and the probe is
 * on the same host. A device sharing them inherits whatever driver binding
 * Windows already made for the probe, so it enumerates correctly and then
 * never appears as a COM port. That is a confusing way to lose an afternoon:
 * the device descriptor is fine, the serial number is right, and nothing
 * reports an error.
 *
 * A shipping board should carry its own allocation. */
#ifndef USB_VID
#define USB_VID 0x1209
#endif
#ifndef USB_PID
#define USB_PID 0x0001
#endif
#ifndef USB_MANUFACTURER
#define USB_MANUFACTURER "WCH"
#endif
#ifndef USB_PRODUCT
#define USB_PRODUCT "CH32H417"
#endif

/* ---- Device descriptor -------------------------------------------------- */

static const tusb_desc_device_t s_device = {
    .bLength            = sizeof(tusb_desc_device_t),
    .bDescriptorType    = TUSB_DESC_DEVICE,
    .bcdUSB             = 0x0200,

    /* Miscellaneous / Common Class / Interface Association Descriptor. A CDC
     * device is two interfaces that must be grouped, and without the IAD
     * Windows binds them separately and the COM port never appears. */
    .bDeviceClass       = TUSB_CLASS_MISC,
    .bDeviceSubClass    = MISC_SUBCLASS_COMMON,
    .bDeviceProtocol    = MISC_PROTOCOL_IAD,

    .bMaxPacketSize0    = CFG_TUD_ENDPOINT0_SIZE,
    .idVendor           = USB_VID,
    .idProduct          = USB_PID,
    .bcdDevice          = 0x0100,
    .iManufacturer      = 0x01,
    .iProduct           = 0x02,
    .iSerialNumber      = 0x03,
    .bNumConfigurations = 0x01,
};

TU_ATTR_WEAK const uint8_t *tud_descriptor_device_cb(void) {
    return (const uint8_t *)&s_device;
}

/* ---- Configuration descriptor -------------------------------------------- */

enum {
    ITF_NUM_CDC = 0,
    ITF_NUM_CDC_DATA,
    ITF_NUM_TOTAL,
};

#define CONFIG_TOTAL_LEN  (TUD_CONFIG_DESC_LEN + TUD_CDC_DESC_LEN)

#define EPNUM_CDC_NOTIF   0x81
#define EPNUM_CDC_OUT     0x02
#define EPNUM_CDC_IN      0x82

static const uint8_t s_config[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, 0x00, 100),
    TUD_CDC_DESCRIPTOR(ITF_NUM_CDC, 4, EPNUM_CDC_NOTIF, 8,
                       EPNUM_CDC_OUT, EPNUM_CDC_IN, 64),
};

TU_ATTR_WEAK const uint8_t *tud_descriptor_configuration_cb(uint8_t index) {
    (void)index;
    return s_config;
}

/* ---- String descriptors -------------------------------------------------- */

static const char *const s_strings[] = {
    (const char[]){0x09, 0x04},   /* 0: English (US) */
    USB_MANUFACTURER,             /* 1 */
    USB_PRODUCT,                  /* 2 */
    NULL,                         /* 3: serial, filled from the unique ID */
    "CH32H417 Serial",            /* 4: CDC interface */
};

static uint16_t s_str_buf[32];

TU_ATTR_WEAK const uint16_t *tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void)langid;

    size_t chars;

    if (index == 0) {
        memcpy(&s_str_buf[1], s_strings[0], 2);
        chars = 1;
    } else if (index == 3) {
        /* The chip's unique ID. Written here rather than held as a literal so
         * two boards never share a serial number -- Windows caches driver
         * bindings per serial, and duplicates make one board inherit the
         * other's COM port assignment. */
        char sn[25];
        ch32h4_usb_serial_number(sn);
        chars = strlen(sn);
        for (size_t i = 0; i < chars; i++) {
            s_str_buf[1 + i] = sn[i];
        }
    } else if (index < TU_ARRAY_SIZE(s_strings)) {
        const char *str = s_strings[index];
        chars = strlen(str);
        if (chars > TU_ARRAY_SIZE(s_str_buf) - 1) {
            chars = TU_ARRAY_SIZE(s_str_buf) - 1;
        }
        for (size_t i = 0; i < chars; i++) {
            s_str_buf[1 + i] = str[i];
        }
    } else {
        return NULL;
    }

    /* Length in bytes, including this header word. */
    s_str_buf[0] = (uint16_t)((TUSB_DESC_STRING << 8) | (2 * chars + 2));
    return s_str_buf;
}
