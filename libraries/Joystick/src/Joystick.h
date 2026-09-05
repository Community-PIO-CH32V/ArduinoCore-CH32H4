/*
    Joystick.h

    Arduino's Joystick/gamepad API, over USB HID.

    The API and the report handling are Benjamin Aigner's, by way of
    arduino-pico's HID_Joystick, which is what HID_Joystick.h next door is.
    The 16-bit gamepad report descriptor in sdkoverride/ is Earle F.
    Philhower III's. Only the transport below is this core's.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    ---
    For the CH32H41x core. Needs the TinyUSB stack, which is the default.

    AXES DEFAULT TO 0..1023, for compatibility with the other Joystick
    libraries. use8bit(), use10bit() and use16bit() change that; the report on
    the wire is 16-bit signed whichever you pick, and the mapping happens on
    this side.

    Updates are sent as they are made. useManualSend(true) batches them and
    send_now() flushes -- which is what you want when several axes and buttons
    change together, because otherwise the host sees an intermediate state
    that never really existed.
*/
#pragma once

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

#include "HID_Joystick.h"

class Joystick_ : public HID_Joystick {
public:
    Joystick_();

    void begin();
    void end();
    bool ready();

    void send_now() override;

private:
    Adafruit_USBD_HID _hid;
    bool _started;
};

extern Joystick_ Joystick;
