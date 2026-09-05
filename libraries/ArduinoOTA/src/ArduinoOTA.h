/*
    ArduinoOTA.h - Simple Arduino IDE OTA handler
    Modified 2022 Earle F. Philhower, III.  All rights reserved.
    Taken from the ESP8266 core libraries, (c) various authors.
    CH32H41x port copyright (c) 2026 Maximilian Gerhardt

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this library; if not, write to the Free Software
    Foundation, Inc., 51 Franklin St, Fifth Floor, Boston, MA  02110-1301  USA

    ---
    THREE THINGS ARE DIFFERENT from the arduino-pico original this came from.

    THE SOCKET. Upstream drives an lwIP raw-API UdpContext from an RX callback.
    This uses EthernetUDP and polls it from handle(), because that is the
    socket this core has -- and because the invite handler starts a transfer
    that runs for seconds, which has no business happening inside an lwIP
    callback.

    THE COMMIT. Upstream stages the image on LittleFS and asks its bootloader
    to apply it at the next boot. There is no bootloader here and no second
    flash slot, so Update.commit() writes the verified image over the running
    sketch from ITCM and resets the part itself.

    THE SIZE LIMIT. Because the image is staged in RAM (see Updater.h), an
    over-the-air image is capped by the heap, not by the flash -- a good deal
    smaller than what a probe can flash. An invite for more than that is
    refused at the invite, before any of it has been transferred, and the IDE
    shows the reason.

    ---
    Usage:

        Ethernet.begin();
        ArduinoOTA.setHostname("ch32h4");   // optional
        ArduinoOTA.setPassword("secret");   // strongly recommended
        ArduinoOTA.begin();
        ...
        void loop() { ArduinoOTA.handle(); }

    WITHOUT A PASSWORD, anyone who can reach the board on the network can
    replace its firmware. That is the upstream default, kept here so that
    sketches behave the same, but it is worth a thought on any network you do
    not control.
*/

#pragma once

#include <Arduino.h>
#include <functional>
#include <Updater.h>
#include <EthernetUdp.h>

typedef enum {
    OTA_IDLE,
    OTA_WAITAUTH,
    OTA_RUNUPDATE
} ota_state_t;

typedef enum {
    OTA_AUTH_ERROR,
    OTA_BEGIN_ERROR,
    OTA_CONNECT_ERROR,
    OTA_RECEIVE_ERROR,
    OTA_END_ERROR
} ota_error_t;


class ArduinoOTAClass {
public:
    typedef std::function<void(void)> THandlerFunction;
    typedef std::function<void(ota_error_t)> THandlerFunction_Error;
    typedef std::function<void(unsigned int, unsigned int)> THandlerFunction_Progress;

    ArduinoOTAClass();
    ~ArduinoOTAClass();

    //Sets the service port. Default 2040
    void setPort(uint16_t port);

    //Sets the device hostname. Default ch32h4-xxxxxx
    void setHostname(const char *hostname);
    String getHostname();

    //Sets the password that will be required for OTA. Default nullptr
    void setPassword(const char *password);

    //Sets the password as above but in the form MD5(password). Default nullptr
    void setPasswordHash(const char *password);

    //Sets if the device should be rebooted after successful update. Default true
    void setRebootOnSuccess(bool reboot);

    //This callback will be called when OTA connection has begun
    void onStart(THandlerFunction fn);

    //This callback will be called when OTA has finished
    void onEnd(THandlerFunction fn);

    //This callback will be called when OTA encountered Error
    void onError(THandlerFunction_Error fn);

    //This callback will be called when OTA is receiving data
    void onProgress(THandlerFunction_Progress fn);

    //Starts the ArduinoOTA service
    void begin(bool useMDNS = true);

    //Ends the ArduinoOTA service
    void end();
    //Call this in loop() to run the service. Also calls MDNS.update() when begin() or begin(true) is used.
    void handle();

    //Gets update command type after OTA has started. Either U_FLASH or U_FS
    int getCommand();

    /* The largest image this board can currently accept, in bytes. A heap
       figure, so it moves with whatever the sketch has allocated -- see the
       note on the size limit at the top of this file. */
    size_t maxImageSize() {
        return UpdaterClass::maxImageSize();
    }

private:
    void _runUpdate(void);
    void _onRx(void);
    int parseInt(void);
    String readStringUntil(char end);

    int _port = 0;
    String _password;
    String _hostname;
    String _nonce;
    /* By value, not by pointer: upstream's UdpContext is reference-counted
       because lwIP callbacks can outlive the object. Nothing here holds a
       reference to it, so it can just be a member. */
    EthernetUDP _udp_ota;
    bool _initialized = false;
    bool _rebootOnSuccess = true;
    bool _useMDNS = true;
    ota_state_t _state = OTA_IDLE;
    int _size = 0;
    int _cmd = 0;
    uint16_t _ota_port = 0;
    uint16_t _ota_udp_port = 0;
    IPAddress _ota_ip;
    String _md5;

    THandlerFunction _start_callback = nullptr;
    THandlerFunction _end_callback = nullptr;
    THandlerFunction_Error _error_callback = nullptr;
    THandlerFunction_Progress _progress_callback = nullptr;
};

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_ARDUINOOTA)
extern ArduinoOTAClass ArduinoOTA;
#endif
