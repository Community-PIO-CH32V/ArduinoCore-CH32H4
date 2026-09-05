/*
    Mouse.h

    Arduino's Mouse API, over USB HID.

    The API is Arduino LLC's and Peter Barrett's, by way of arduino-pico's
    HID_Mouse, which is what HID_Mouse.h next door is. Only the transport
    below is this core's.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    ---
    For the CH32H41x core.

    NEEDS THE TinyUSB STACK, which is the default.

    A SKETCH THAT MOVES THE POINTER CAN MAKE THE BOARD HARD TO REFLASH -- a
    cursor that will not stay still is worse than a keyboard that types, and
    it happens the moment the board enumerates. Gate it on something, as the
    examples do.
*/
#pragma once

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

#include "HID_Mouse.h"

class Mouse_ : public HID_Mouse {
public:
    Mouse_();

    void begin();
    void end();
    bool ready();

    /* Relative motion, -127..127 per axis. Larger asks are split across
       several reports by HID_Mouse. */
    void move(int x, int y, signed char wheel = 0) override;

private:
    Adafruit_USBD_HID _hid;
    bool _started;
};

extern Mouse_ Mouse;
