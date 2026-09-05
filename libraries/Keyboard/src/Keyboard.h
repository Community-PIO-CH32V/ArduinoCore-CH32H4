/*
    Keyboard.h

    Arduino's Keyboard API, over USB HID.

    The API, the keycodes and the layout tables are Arduino LLC's and Peter
    Barrett's, by way of arduino-pico's HID_Keyboard, which is what
    HID_Keyboard.h next door is. Only the transport below is this core's.

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    ---
    For the CH32H41x core.

    NEEDS THE TinyUSB STACK, which is the default. With the USB stack menu set
    to None there is no HID to speak through and begin() does nothing.

    A SKETCH THAT PRESSES KEYS CAN LOCK YOU OUT OF IT. The board enumerates as
    a keyboard and starts typing into whatever has focus, and if it does that
    in loop() with no delay you cannot get a window in edgeways to reflash it.
    Gate it on something -- a button, a serial command -- as the examples do.
    Recovering otherwise means holding the board in its bootloader.
*/
#pragma once

#include <Arduino.h>
#include <Adafruit_TinyUSB.h>

#include "HID_Keyboard.h"

class Keyboard_ : public HID_Keyboard {
public:
    Keyboard_();

    /* Starts the HID interface as well as the key state. Call it from
       setup(); the host takes a moment to enumerate, so the first keystroke
       should not follow immediately. */
    void begin(const uint8_t *layout = KeyboardLayout_en_US);
    void end();

    /* True once the host has configured the interface and is accepting
       reports. Sending before this returns false is not an error; it is a
       report nobody receives. */
    bool ready();

protected:
    void sendReport(KeyReport *keys) override;
    void sendConsumerReport(uint16_t key) override;

private:
    Adafruit_USBD_HID _hid;
    bool _started;
};

extern Keyboard_ Keyboard;
