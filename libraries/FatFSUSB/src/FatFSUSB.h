/*
    FatFSUSB.h - Expose the internal flash FAT volume to a PC as a USB stick

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

    ---
    The API is upstream's. What differs underneath: this sits on
    Adafruit_USBD_MSC, which this core already has and which owns the USB
    descriptor, rather than on raw TinyUSB descriptors -- so the whole
    descriptor-building half of upstream's implementation is gone.

        FatFS.begin();
        FatFSUSB.onPlug(onPlug);
        FatFSUSB.onUnplug(onUnplug);
        FatFSUSB.begin();

    THE SKETCH AND THE HOST MUST NOT BOTH OWN THE VOLUME.

    FatFs caches FAT and directory sectors. A host writing underneath that
    cache corrupts one or both views, and does it silently -- the sketch keeps
    serving files from a directory that no longer exists on the medium. There
    is no way to share a FAT volume between two writers without a locking
    protocol neither side has, so the contract is that only one has it at a
    time:

        onPlug   -> the host has mounted it. The sketch calls FatFS.end().
        onUnplug -> the host ejected. The sketch calls FatFS.begin() again and
                    re-reads anything it cares about.

    THOSE TWO ARE BEST-EFFORT AND DO NOT FIRE ON EVERY HOST. They come from
    SCSI START STOP UNIT and PREVENT/ALLOW MEDIUM REMOVAL, and Windows was
    measured mounting this volume, writing a file and ejecting it without
    sending either -- the plug and unplug counts stayed at zero throughout.
    A sketch that assumes the callbacks will arrive is a sketch that corrupts
    its filesystem on Windows.

    hostChanged() is what a sketch should actually watch. It goes true when the
    host mounts (where the host announces it) or writes (which it cannot do
    silently), so it works whether or not the callbacks fire -- and on Windows
    they do not. The callbacks remain useful where a host does send them,
    because they fire on the mount rather than on the first write.

    driveReady() lets the sketch refuse the host while it is mid-operation.
    Returning false reports "not ready", which every host handles.

    Mass storage goes to the translation layer directly, never through FatFs:
    while the host has the volume, it owns the filesystem structure, and
    FatFs' cached sectors would disagree with what the host just wrote.
*/
#pragma once

#include <Arduino.h>

class FatFSUSBClass {
public:
    FatFSUSBClass() { }
    ~FatFSUSBClass() { end(); }

    /* Presents the flash volume as a USB drive.
     *
     * FatFS.begin() (or .format()) must have run first: the volume's size
     * comes from the translation layer, and there is nothing to present
     * before it exists. Returns false if it has not. */
    bool begin();
    void end();

    void driveReady(bool (*cb)(uint32_t), uint32_t cbData = 0) {
        _driveReady = cb;
        _driveReadyData = cbData;
    }
    void onPlug(void (*cb)(uint32_t), uint32_t cbData = 0) {
        _cbPlug = cb;
        _cbPlugData = cbData;
    }
    void onUnplug(void (*cb)(uint32_t), uint32_t cbData = 0) {
        _cbUnplug = cb;
        _cbUnplugData = cbData;
    }

    bool started() const { return _started; }

    /* The longest a single mass-storage write has blocked, in microseconds.
     *
     * Worth having rather than guessing: a write lands in the TinyUSB task and
     * calls into flash, which parks the other core and masks interrupts for
     * the duration of an 8 KB erase. Bulk transfers tolerate delay, but this
     * is exactly the kind of thing that works against one host controller and
     * fails against another, so the number is measurable instead of assumed.
     */
    uint32_t worstWriteMicros() const { return _worstWriteUs; }
    void resetWorstWriteMicros() { _worstWriteUs = 0; }

    /* HAS THE HOST TOUCHED THE VOLUME? The one call a sketch needs.
     *
     * True once the host has mounted it (where the host says so) or written to
     * it (which it cannot do silently), whichever happened first. Both routes
     * mean the same thing -- this sketch's FatFs cache no longer matches the
     * medium -- so they are one flag rather than two things to watch.
     *
     * Sticky until clearHostChanged(), so a sketch polling from loop() cannot
     * miss it between calls:
     *
     *     if (FatFSUSB.hostChanged()) {
     *         FatFSUSB.clearHostChanged();
     *         FatFS.end();            // or end-then-begin to re-read it
     *     }
     */
    bool hostChanged() const { return _hostChanged; }
    void clearHostChanged() { _hostChanged = false; }

    /* How many sectors the host has written since boot.
     *
     * The raw counter behind hostChanged(), for a sketch that wants to know
     * how much rather than whether -- a log that only rewrites itself when the
     * host actually put something there, say. A write is a write, whatever the
     * host did or did not announce. */
    uint32_t hostWrites() const { return _hostWrites; }

    /* Called from the MSC callbacks; not for sketches. */
    int32_t read10(uint32_t lba, void *buffer, uint32_t bufsize);
    int32_t write10(uint32_t lba, uint8_t *buffer, uint32_t bufsize);
    void flush10();
    bool testUnitReady();
    void plug();
    void unplug();

private:
    bool _started = false;
    bool _plugged = false;
    uint32_t _worstWriteUs = 0;
    uint32_t _hostWrites = 0;
    bool _hostChanged = false;

    void (*_cbPlug)(uint32_t) = nullptr;
    uint32_t _cbPlugData = 0;
    void (*_cbUnplug)(uint32_t) = nullptr;
    uint32_t _cbUnplugData = 0;
    bool (*_driveReady)(uint32_t) = nullptr;
    uint32_t _driveReadyData = 0;
};

extern FatFSUSBClass FatFSUSB;
