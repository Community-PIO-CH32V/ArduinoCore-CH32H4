/*
    ArduinoOTA.cpp - Simple Arduino IDE OTA handler
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

    See ArduinoOTA.h for what differs from the arduino-pico original.
*/

#include <functional>
#include "ArduinoOTA.h"
#include <MD5Builder.h>
#include <StreamString.h>
#include <EthernetClient.h>
#include <MDNS.h>

#ifdef DEBUG_CH32H4_CORE
#ifdef DEBUG_CH32H4_PORT
#define OTA_DEBUG DEBUG_CH32H4_PORT
#endif
#endif

ArduinoOTAClass::ArduinoOTAClass() {
}

ArduinoOTAClass::~ArduinoOTAClass() {
    _udp_ota.stop();
}

void ArduinoOTAClass::onStart(THandlerFunction fn) {
    _start_callback = fn;
}

void ArduinoOTAClass::onEnd(THandlerFunction fn) {
    _end_callback = fn;
}

void ArduinoOTAClass::onProgress(THandlerFunction_Progress fn) {
    _progress_callback = fn;
}

void ArduinoOTAClass::onError(THandlerFunction_Error fn) {
    _error_callback = fn;
}

void ArduinoOTAClass::setPort(uint16_t port) {
    if (!_initialized && !_port && port) {
        _port = port;
    }
}

void ArduinoOTAClass::setHostname(const char * hostname) {
    if (!_initialized && !_hostname.length() && hostname) {
        _hostname = hostname;
    }
}

String ArduinoOTAClass::getHostname() {
    return _hostname;
}

void ArduinoOTAClass::setPassword(const char * password) {
    if (!_initialized && !_password.length() && password) {
        MD5Builder passmd5;
        passmd5.begin();
        passmd5.add(password);
        passmd5.calculate();
        _password = passmd5.toString();
    }
}

void ArduinoOTAClass::setPasswordHash(const char * password) {
    if (!_initialized && !_password.length() && password) {
        _password = password;
    }
}

void ArduinoOTAClass::setRebootOnSuccess(bool reboot) {
    _rebootOnSuccess = reboot;
}

/* Upstream appends into the UdpContext's pending packet and then sends it.
   EthernetUDP builds a datagram between beginPacket() and endPacket(), so the
   two-step becomes one call. */
static void otaReply(EthernetUDP &udp, IPAddress ip, uint16_t port,
                     const char *a, size_t alen,
                     const char *b = nullptr, size_t blen = 0) {
    udp.beginPacket(ip, port);
    udp.write((const uint8_t *)a, alen);
    if (b && blen) {
        udp.write((const uint8_t *)b, blen);
    }
    udp.endPacket();
}

void ArduinoOTAClass::begin(bool useMDNS) {
    if (_initialized) {
        return;
    }

    _useMDNS = useMDNS;

    if (!_hostname.length()) {
        /* The last three bytes of the chip's unique ID, so that two boards on
           one network do not both answer to the same name. Upstream spells
           this pico-<chip id>. */
        char tmp[24];
        uint8_t uid[8];
        CH32H4.getUniqueId(uid);
        sprintf(tmp, "ch32h4-%02x%02x%02x", uid[5], uid[6], uid[7]);
        _hostname = tmp;
    }
    if (!_port) {
        _port = 2040;
    }

    _udp_ota.stop();
    if (!_udp_ota.begin(_port)) {
        return;
    }

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_MDNS)
    if (_useMDNS) {
        MDNS.begin(_hostname.c_str());

        if (_password.length()) {
            MDNS.enableArduino(_port, true);
        } else {
            MDNS.enableArduino(_port);
        }
    }
#endif
    _initialized = true;
    _state = OTA_IDLE;
#ifdef OTA_DEBUG
    OTA_DEBUG.printf("OTA server at: %s.local:%u\n", _hostname.c_str(), _port);
#endif
}

int ArduinoOTAClass::parseInt() {
    char data[16];
    uint8_t index;
    char value;
    while (_udp_ota.peek() == ' ') {
        _udp_ota.read();
    }
    for (index = 0; index < sizeof(data); ++index) {
        value = _udp_ota.peek();
        if (value < '0' || value > '9') {
            data[index] = '\0';
            return atoi(data);
        }
        data[index] = _udp_ota.read();
    }
    return 0;
}

String ArduinoOTAClass::readStringUntil(char end) {
    String res;
    int value;
    while (true) {
        value = _udp_ota.read();
        if (value < 0 || value == '\0' || value == end) {
            return res;
        }
        res += static_cast<char>(value);
    }
    return res;
}

void ArduinoOTAClass::_onRx() {
    /* Upstream is called back by lwIP with a packet already current, and calls
       next() to claim it. Polled, parsePacket() is both: it returns the size of
       the next datagram and makes it the one read() draws from. */
    if (_udp_ota.parsePacket() <= 0) {
        return;
    }
    IPAddress ota_ip;

    if (_state == OTA_IDLE) {
        int cmd = parseInt();
        if (cmd != U_FLASH && cmd != U_FS) {
            return;
        }
        _ota_ip = _udp_ota.remoteIP();
        _cmd  = cmd;
        _ota_port = parseInt();
        _ota_udp_port = _udp_ota.remotePort();
        _size = parseInt();
        _udp_ota.read();
        _md5 = readStringUntil('\n');
        _md5.trim();
        if (_md5.length() != 32) {
            return;
        }

        ota_ip = _ota_ip;

        if (_password.length()) {
            MD5Builder nonce_md5;
            nonce_md5.begin();
            nonce_md5.add(String(micros()));
            nonce_md5.calculate();
            _nonce = nonce_md5.toString();

            char auth_req[38];
            sprintf(auth_req, "AUTH %s", _nonce.c_str());
            otaReply(_udp_ota, ota_ip, _ota_udp_port, auth_req, strlen(auth_req));
            _state = OTA_WAITAUTH;
            return;
        } else {
            _state = OTA_RUNUPDATE;
        }
    } else if (_state == OTA_WAITAUTH) {
        int cmd = parseInt();
        if (cmd != U_AUTH) {
            _state = OTA_IDLE;
            return;
        }
        _udp_ota.read();
        String cnonce = readStringUntil(' ');
        String response = readStringUntil('\n');
        if (cnonce.length() != 32 || response.length() != 32) {
            _state = OTA_IDLE;
            return;
        }

        String challenge = _password + ':' + String(_nonce) + ':' + cnonce;
        MD5Builder _challengemd5;
        _challengemd5.begin();
        _challengemd5.add(challenge);
        _challengemd5.calculate();
        String result = _challengemd5.toString();

        ota_ip = _ota_ip;
        //    if(result.equalsConstantTime(response)) {
        if (result.equals(response)) {
            _state = OTA_RUNUPDATE;
        } else {
            otaReply(_udp_ota, ota_ip, _ota_udp_port, "Authentication Failed", 21);
            if (_error_callback) {
                _error_callback(OTA_AUTH_ERROR);
            }
            _state = OTA_IDLE;
        }
    }

    /* Anything else queued behind it is stale. */
    while (_udp_ota.parsePacket() > 0) {
        _udp_ota.flush();
    }
}

void ArduinoOTAClass::_runUpdate() {
    IPAddress ota_ip = _ota_ip;

    /* Where upstream checks that LittleFS mounted, because that is where it
       stages. This stages in RAM, so the question is whether the heap can hold
       the image -- and the honest answer is worth sending back, since "Not
       Enough Space" from the Updater does not say space in what. */
    if (_cmd == U_FLASH) {
        size_t maxSize = UpdaterClass::maxImageSize();
        if ((size_t)_size > maxSize) {
            char err[96];
            snprintf(err, sizeof(err),
                     "Image is %d bytes, only %u bytes of RAM free to stage it in",
                     _size, (unsigned)maxSize);
#ifdef OTA_DEBUG
            OTA_DEBUG.println(err);
#endif
            if (_error_callback) {
                _error_callback(OTA_BEGIN_ERROR);
            }
            otaReply(_udp_ota, ota_ip, _ota_udp_port, "ERR: ", 5, err, strlen(err));
            delay(100);
            _udp_ota.begin(_port);
            _state = OTA_IDLE;
            return;
        }
    }

    if (!Update.begin(_size, _cmd)) {
#ifdef OTA_DEBUG
        OTA_DEBUG.println("Update Begin Error");
#endif
        if (_error_callback) {
            _error_callback(OTA_BEGIN_ERROR);
        }

        StreamString ss;
        Update.printError(ss);
        otaReply(_udp_ota, ota_ip, _ota_udp_port, "ERR: ", 5, ss.c_str(), ss.length());
        delay(100);
        _udp_ota.begin(_port);
        _state = OTA_IDLE;
        return;
    }

    otaReply(_udp_ota, ota_ip, _ota_udp_port, "OK", 2);
    delay(100);

    Update.setMD5(_md5.c_str());

    if (_start_callback) {
        _start_callback();
    }
    if (_progress_callback) {
        _progress_callback(0, _size);
    }

    EthernetClient client;
    if (!client.connect(_ota_ip, _ota_port)) {
#ifdef OTA_DEBUG
        OTA_DEBUG.printf("Connect Failed\n");
#endif
        _udp_ota.begin(_port);
        if (_error_callback) {
            _error_callback(OTA_CONNECT_ERROR);
        }
        _state = OTA_IDLE;
        /* Upstream falls through into the transfer loop here with a client
           that never connected. It survives that because the loop's first
           condition is false, but it leaves an Update running that nothing
           ends. Return, and clear it. */
        Update.end(true);
        return;
    }
    // OTA sends little packets
    client.setNoDelay(true);

    uint32_t written, total = 0;
    while (!Update.isFinished() && (client.connected() || client.available())) {
        int waited = 1000;
        while (!client.available() && waited--) {
            delay(1);
        }
        if (!waited) {
#ifdef OTA_DEBUG
            OTA_DEBUG.printf("Receive Failed\n");
#endif
            _udp_ota.begin(_port);
            if (_error_callback) {
                _error_callback(OTA_RECEIVE_ERROR);
            }
            _state = OTA_IDLE;
            /* As above: upstream keeps looping on a stream that has stopped
               producing, and only leaves when the peer drops. */
            client.stop();
            Update.end(true);
            return;
        }
        written = Update.write(client);
        if (written > 0) {
            client.print(written, DEC);
            total += written;
            if (_progress_callback) {
                _progress_callback(total, _size);
            }
        }
    }


    if (Update.end()) {
        // Ensure last count packet has been sent out and not combined with the final OK
        client.flush();
        delay(1000);
        client.print("OK");
        client.flush();
        delay(1000);
        client.stop();
#ifdef OTA_DEBUG
        OTA_DEBUG.printf("Update Success\n");
#endif
        if (_end_callback) {
            _end_callback();
        }
        if (_rebootOnSuccess) {
#ifdef OTA_DEBUG
            OTA_DEBUG.printf("Committing...\n");
#endif
            //let serial/network finish tasks that might be given in _end_callback
            delay(100);
            /* Where upstream reboots into its bootloader. This writes the
               staged image over the running sketch and resets the part, so it
               DOES NOT RETURN -- unless it could not park the other core, in
               which case nothing has been touched and the old sketch is still
               there to say so. */
            if (!Update.commit()) {
#ifdef OTA_DEBUG
                OTA_DEBUG.printf("Commit failed, firmware unchanged\n");
#endif
                if (_error_callback) {
                    _error_callback(OTA_END_ERROR);
                }
                _udp_ota.begin(_port);
                _state = OTA_IDLE;
            }
        }
    } else {
        _udp_ota.begin(_port);
        if (_error_callback) {
            _error_callback(OTA_END_ERROR);
        }
        Update.printError(client);
#ifdef OTA_DEBUG
        Update.printError(OTA_DEBUG);
#endif
        _state = OTA_IDLE;
    }
}

void ArduinoOTAClass::end() {
    _initialized = false;
    _udp_ota.stop();
#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_MDNS)
    if (_useMDNS) {
        MDNS.end();
    }
#endif
    _state = OTA_IDLE;
#ifdef OTA_DEBUG
    OTA_DEBUG.printf("OTA server stopped.\n");
#endif
}
//this needs to be called in the loop()
void ArduinoOTAClass::handle() {
    /* Upstream gets here from an lwIP RX callback. Polled instead: an invite
       starts a transfer that runs for seconds, which is not something to do
       inside a stack callback. */
    if (_initialized) {
        _onRx();
    }

    if (_state == OTA_RUNUPDATE) {
        _runUpdate();
        _state = OTA_IDLE;
    }

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_MDNS)
    if (_useMDNS) {
        MDNS.update();    //handle MDNS update as well, given that ArduinoOTA relies on it anyways
    }
#endif
}

int ArduinoOTAClass::getCommand() {
    return _cmd;
}

#if !defined(NO_GLOBAL_INSTANCES) && !defined(NO_GLOBAL_ARDUINOOTA)
ArduinoOTAClass ArduinoOTA;
#endif
