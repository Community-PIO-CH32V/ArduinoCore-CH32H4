/*
    Mouse.cpp -- the USB transport under Arduino's Mouse API.

    The API is Arduino LLC's and Peter Barrett's; see Mouse.h.
    LGPL-2.1-or-later.
*/
#include "Mouse.h"

enum { RID_MOUSE = 1 };

static const uint8_t desc_hid_report[] = {
    TUD_HID_REPORT_DESC_MOUSE(HID_REPORT_ID(RID_MOUSE)),
};

Mouse_::Mouse_()
    : HID_Mouse(false),
      _hid(desc_hid_report, sizeof(desc_hid_report), HID_ITF_PROTOCOL_MOUSE,
           2, false),
      _started(false) {
}

void Mouse_::begin() {
    if (!_started) {
        _hid.begin();
        _started = true;
    }
    HID_Mouse::begin();
}

void Mouse_::end() {
    HID_Mouse::end();
}

bool Mouse_::ready() {
    return _started && _hid.ready();
}

void Mouse_::move(int x, int y, signed char wheel) {
    if (!_started) {
        return;
    }
    const uint32_t deadline = millis() + 100;
    while (!_hid.ready() && millis() < deadline) {
        yield();
    }
    _hid.mouseReport(RID_MOUSE, _buttons, (int8_t)limit_xy(x),
                     (int8_t)limit_xy(y), wheel, 0);
}

Mouse_ Mouse;
