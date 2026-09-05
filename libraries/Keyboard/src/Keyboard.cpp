/*
    Keyboard.cpp -- the USB transport under Arduino's Keyboard API.

    The API and the key tables are Arduino LLC's and Peter Barrett's; see
    Keyboard.h. LGPL-2.1-or-later.
*/
#include "Keyboard.h"

/* Two report IDs on one interface: the keyboard proper and the consumer-control
   page, which is what carries volume, play/pause and the browser keys. They
   have to be distinguishable, and a report ID is how. */
enum {
    RID_KEYBOARD = 1,
    RID_CONSUMER = 2,
};

static const uint8_t desc_hid_report[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(HID_REPORT_ID(RID_KEYBOARD)),
    TUD_HID_REPORT_DESC_CONSUMER(HID_REPORT_ID(RID_CONSUMER)),
};

Keyboard_::Keyboard_()
    : _hid(desc_hid_report, sizeof(desc_hid_report), HID_ITF_PROTOCOL_KEYBOARD,
           2, false),
      _started(false) {
}

void Keyboard_::begin(const uint8_t *layout) {
    if (!_started) {
        /* Before TinyUSB_Device_Init() has run, this only registers the
           interface; the descriptor is assembled when the device starts. */
        _hid.begin();
        _started = true;
    }
    HID_Keyboard::begin(layout);
}

void Keyboard_::end() {
    HID_Keyboard::end();
}

bool Keyboard_::ready() {
    return _started && _hid.ready();
}

void Keyboard_::sendReport(KeyReport *keys) {
    if (!_started) {
        return;
    }
    /* Wait for the endpoint rather than dropping the report. A dropped press
       leaves the host believing a key is still down, which is worse than a
       few milliseconds of delay -- and the bound stops a sketch from hanging
       when nothing is listening. */
    const uint32_t deadline = millis() + 100;
    while (!_hid.ready() && millis() < deadline) {
        yield();
    }
    _hid.keyboardReport(RID_KEYBOARD, keys->modifiers, keys->keys);
}

void Keyboard_::sendConsumerReport(uint16_t key) {
    if (!_started) {
        return;
    }
    const uint32_t deadline = millis() + 100;
    while (!_hid.ready() && millis() < deadline) {
        yield();
    }
    _hid.sendReport16(RID_CONSUMER, key);
}

Keyboard_ Keyboard;
