/*
    Joystick.cpp -- the USB transport under the Joystick API.

    The API is Benjamin Aigner's; see Joystick.h. LGPL-2.1-or-later.
*/
#include "Joystick.h"

enum { RID_GAMEPAD = 1 };

static const uint8_t desc_hid_report[] = {
    TUD_HID_REPORT_DESC_GAMEPAD16(HID_REPORT_ID(RID_GAMEPAD)),
};

Joystick_::Joystick_()
    : _hid(desc_hid_report, sizeof(desc_hid_report), HID_ITF_PROTOCOL_NONE,
           2, false),
      _started(false) {
}

void Joystick_::begin() {
    if (!_started) {
        _hid.begin();
        _started = true;
    }
    HID_Joystick::begin();
}

void Joystick_::end() {
    HID_Joystick::end();
}

bool Joystick_::ready() {
    return _started && _hid.ready();
}

void Joystick_::send_now() {
    if (!_started) {
        return;
    }
    /* Wait for the endpoint rather than dropping the report: a dropped button
       press leaves the host holding it down. Bounded, so a sketch does not
       hang when nothing is listening. */
    const uint32_t deadline = millis() + 100;
    while (!_hid.ready() && millis() < deadline) {
        yield();
    }
    hid_gamepad16_report_t report;
    getReport(&report);
    _hid.sendReport(RID_GAMEPAD, &report, sizeof(report));
}

Joystick_ Joystick;
