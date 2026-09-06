/*
    FatFSUSB.cpp - Expose the internal flash FAT volume to a PC as a USB stick

    Copyright (c) 2024 Earle F. Philhower, III <earlephilhower@yahoo.com>
    CH32H41x port copyright (c) 2026 Community-PIO-CH32V

    This library is free software; you can redistribute it and/or
    modify it under the terms of the GNU Lesser General Public
    License as published by the Free Software Foundation; either
    version 2.1 of the License, or (at your option) any later version.

    This library is distributed in the hope that it will be useful,
    but WITHOUT ANY WARRANTY; without even the implied warranty of
    MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the GNU
    Lesser General Public License for more details.

    You should have received a copy of the GNU Lesser General Public
    License along with this program. If not, see https://www.gnu.org/licenses/

    See FatFSUSB.h for the ownership contract, which is the part that matters.
*/
#include "FatFSUSB.h"

#include <FatFS.h>
#include <Adafruit_TinyUSB.h>

static Adafruit_USBD_MSC s_msc;

/* The MSC layer hands out plain C function pointers, so these forward to the
   single instance. There is one flash volume and therefore one of these. */
static int32_t mscRead(uint32_t lba, void *buffer, uint32_t bufsize) {
    return FatFSUSB.read10(lba, buffer, bufsize);
}

static int32_t mscWrite(uint32_t lba, uint8_t *buffer, uint32_t bufsize) {
    return FatFSUSB.write10(lba, buffer, bufsize);
}

static void mscFlush(void) {
    FatFSUSB.flush10();
}

static bool mscReady(void) {
    return FatFSUSB.testUnitReady();
}

static bool mscStartStop(uint8_t power_condition, bool start, bool load_eject) {
    (void)power_condition;
    /* This is where a host says it has taken the volume or given it back.
       load_eject distinguishes "spin up/down" from "mount/eject"; only the
       latter changes who owns the filesystem. */
    if (load_eject) {
        if (start) {
            FatFSUSB.plug();
        } else {
            FatFSUSB.unplug();
        }
    }
    return true;
}

/* WHO OWNS THE MEDIUM.
 *
 * setStartStopCallback() alone is not enough: Windows mounts a removable
 * volume without ever sending START STOP UNIT, so a sketch relying on it is
 * never told the host has taken the filesystem. Measured -- plug and unplug
 * counts stayed at zero through a real mount, write and eject.
 *
 * PREVENT/ALLOW MEDIUM REMOVAL is the command Windows does send: it locks the
 * medium before touching it and unlocks it on eject. TinyUSB answers that one
 * itself and offers this weak callback for it, so overriding tud_msc_scsi_cb
 * -- which is what arduino-pico does, and what was tried here first -- never
 * sees it at all: the generic hook only runs for commands the stack does not
 * already handle.
 */
extern "C" bool tud_msc_prevent_allow_medium_removal_cb(uint8_t lun,
                                                        uint8_t prohibit_removal,
                                                        uint8_t control) {
    (void)lun;
    (void)control;
    if (prohibit_removal) {
        FatFSUSB.plug();
    } else {
        FatFSUSB.unplug();
    }
    return true;
}

bool FatFSUSBClass::begin() {
    if (_started) {
        return true;
    }

    /* The size comes from the translation layer, so the filesystem has to
     * exist before it can be presented. Refusing here is much clearer than
     * enumerating a zero-byte drive, which a host reports as unreadable
     * media and which looks like broken hardware. */
    const uint32_t blocks = ch32h4_fatfs_lba_count();
    if (!blocks) {
        return false;
    }

    /* Eight, sixteen and four characters, space padded, per the SCSI inquiry
       response. What the host shows in its device list. */
    s_msc.setID("CH32H4", "Flash Filesystem", "1.0");
    s_msc.setCapacity(blocks, 512);
    s_msc.setReadWriteCallback(mscRead, mscWrite, mscFlush);
    s_msc.setReadyCallback(mscReady);
    s_msc.setStartStopCallback(mscStartStop);
    s_msc.setUnitReady(true);

    /* Before begin(), not after: begin() publishes the interface and the host
       can call straight back into these callbacks. */
    _started = true;
    if (!s_msc.begin()) {
        _started = false;
        return false;
    }

    /* RE-ENUMERATE IF THE HOST IS ALREADY ATTACHED.
     *
     * This core brings USB up before setup() runs, so by the time a sketch
     * calls begin() the host may already hold a configuration descriptor --
     * and adding an interface only appends to the descriptor buffer, which
     * changes nothing the host has already read. Without this, the drive
     * simply never appears, with no error anywhere to say why.
     *
     * Bouncing the connection makes the host ask again. It costs the CDC
     * serial port too, briefly: any terminal open on it will see the port
     * disappear and come back, so a sketch that wants to avoid that should
     * call begin() from setup(), before enumeration completes. */
    if (TinyUSBDevice.mounted()) {
        TinyUSBDevice.detach();
        delay(20);
        TinyUSBDevice.attach();
    }

    return true;
}

void FatFSUSBClass::end() {
    if (!_started) {
        return;
    }
    /* Report the medium gone rather than tearing the interface down: the
       descriptor was published at enumeration and cannot be withdrawn without
       making the host re-enumerate. "No media" is what a card reader with no
       card reports, and hosts handle it. */
    s_msc.setUnitReady(false);
    _started = false;
    _plugged = false;
}

int32_t FatFSUSBClass::read10(uint32_t lba, void *buffer, uint32_t bufsize) {
    /* Straight to the translation layer. While a host has this volume
     * mounted it owns the filesystem structure, and going through FatFs would
     * hand back its cached FAT and directory sectors -- which the host has
     * already changed underneath. */
    if (bufsize % 512) {
        return -1;
    }
    uint8_t *p = (uint8_t *)buffer;
    for (uint32_t i = 0; i < bufsize / 512; i++) {
        if (!ch32h4_fatfs_lba_read(lba + i, p + i * 512)) {
            return -1;
        }
    }
    return (int32_t)bufsize;
}

int32_t FatFSUSBClass::write10(uint32_t lba, uint8_t *buffer, uint32_t bufsize) {
    if (bufsize % 512) {
        return -1;
    }
    const uint32_t t0 = micros();
    _hostWrites += bufsize / 512;
    for (uint32_t i = 0; i < bufsize / 512; i++) {
        if (!ch32h4_fatfs_lba_write(lba + i, buffer + i * 512)) {
            return -1;
        }
    }
    /* See worstWriteMicros(): this runs in the TinyUSB task and a flash erase
       underneath it stalls USB for its duration. Measured rather than
       assumed. */
    const uint32_t el = micros() - t0;
    if (el > _worstWriteUs) {
        _worstWriteUs = el;
    }
    return (int32_t)bufsize;
}

void FatFSUSBClass::flush10() {
    /* The host has finished a burst. Push the mapping down, so the volume is
       consistent on the medium and not merely in RAM. */
    ch32h4_fatfs_lba_sync();
}

bool FatFSUSBClass::testUnitReady() {
    /* Whether there is MEDIUM, not whether begin() has returned.
     *
     * The host asks this during enumeration -- before begin() has finished --
     * and answering "no media" then makes Windows record the disk as No Media
     * and stop asking. The honest answer is whether the translation layer has
     * blocks to serve, which is true from the moment FatFS.begin() ran. */
    if (_driveReady) {
        return _driveReady(_driveReadyData);
    }
    return ch32h4_fatfs_lba_count() > 0;
}

void FatFSUSBClass::plug() {
    if (_plugged) {
        return;
    }
    _plugged = true;
    if (_cbPlug) {
        _cbPlug(_cbPlugData);
    }
}

void FatFSUSBClass::unplug() {
    if (!_plugged) {
        return;
    }
    _plugged = false;
    /* Everything the host wrote reaches the medium before the sketch is told
       it may mount again -- otherwise begin() would read a volume missing its
       last few sectors. */
    ch32h4_fatfs_lba_sync();
    if (_cbUnplug) {
        _cbUnplug(_cbUnplugData);
    }
}

FatFSUSBClass FatFSUSB;
