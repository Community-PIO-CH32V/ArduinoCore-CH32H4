# Third-party components

This core bundles a lot of other people's work so that it builds from a single
checkout. **None of it is covered by this repository's `LICENSE`.** Each entry
below keeps the licence its authors chose, and every such file carries its own
notice in its header.

The list is grouped by what the licence obliges you to do, because that is the
question anyone reading this file actually has.

## Copyleft: LGPL-2.1

The combined distribution carries these obligations. This is the reason the
repository as a whole cannot be redistributed under MIT alone.

| Component | Where | Origin |
|---|---|---|
| ArduinoCore-API | `ArduinoCore-API/` | Arduino. The API every sketch is written against. |
| `FS.h`, `FSImpl.h` | `cores/ch32h4/` | Ivan Grokhotkov, ESP8266 Arduino core |
| HTTPClient | `libraries/HTTPClient/` | Markus Sattler, ESP8266 Arduino core |
| WebServer | `libraries/WebServer/` | Ivan Grokhotkov, ESP8266 Arduino core |
| Updater, FatFSUSB | `libraries/` | Earle F. Philhower III, arduino-pico |
| Joystick | `libraries/Joystick/` | Benjamin Aigner; Earle F. Philhower III |
| Keyboard, Mouse | `libraries/` | Peter Barrett; Arduino LLC |
| FatFS wrapper | `libraries/FatFS/` | wrapper layer from arduino-pico; the FatFs core itself is ChaN's, below |

## Permissive

| Component | Licence | Where | Origin |
|---|---|---|---|
| mbedTLS | Apache-2.0 | `libraries/mbedtls/` | Trusted Firmware / Arm |
| lwIP | BSD-3-Clause | `libraries/lwip/`, `lwIP_Ethernet/`, `lwIP_MDNS/` | Swedish Institute of Computer Science and contributors |
| littlefs | BSD-3-Clause | `libraries/LittleFS/` | Arm Limited; the littlefs authors |
| FatFs | BSD-style, 1-clause | `libraries/FatFS/` | ChaN |
| Adafruit TinyUSB Arduino | MIT | `libraries/Adafruit_TinyUSB_Arduino/` | Ha Thach for Adafruit Industries |
| Adafruit SleepyDog | MIT | `libraries/Adafruit_SleepyDog/` | Adafruit Industries |
| http-parser | MIT | `libraries/http-parser/` | Joyent and Node.js contributors |

## Vendor SDK: not a free-software licence

| Component | Where | Terms |
|---|---|---|
| CH32H417 peripheral library, startup and system files | `system/ch32h417lib/`, `cores/ch32h4/system_ch32h417*.{c,h}`, `cores/ch32h4/ch32h417_conf.h` | Nanjing Qinheng Microelectronics (WCH), 2025 |

WCH's notice reads, in every file:

> This software (modified or not) and binary are used for microcontroller
> manufactured by Nanjing Qinheng Microelectronics.

That is a **field-of-use restriction**: the code may be used with WCH parts and
not otherwise. It is not open source under the OSI definition, and it cannot be
relicensed or reused on another vendor's silicon. Anyone repackaging this core
should read it as written rather than assuming it behaves like the permissive
licences above.

## Planned

| Component | Licence | Note |
|---|---|---|
| libhelix-mp3 | RealNetworks RPSL/RCSL | To be vendored under `libraries/MP3Audio/src/libhelix-mp3/` for the MP3 web radio work. Chosen knowingly over public-domain minimp3 for its fixed-point arithmetic and its record on ESP8266 and ESP32. It is neither MIT nor BSD, and it will be the only copyleft-style component that is not already LGPL. See `docs/superpowers/specs/2026-09-09-mp3-webradio-design.md`. |

## Keeping this accurate

`LICENSE` is written so that this file cannot silently go stale: it covers only
files with **no** copyright notice of their own, so adding a third-party file
with its header intact automatically excludes it, whether or not it is listed
here. Please still list it — the point of this file is that a reader should not
have to grep the tree to answer "what am I agreeing to".
