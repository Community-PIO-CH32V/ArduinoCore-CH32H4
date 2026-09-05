/*
    Updater - Handles FS or app updates
    Adapted from arduino-pico's Updater class, itself adapted from the
    ESP8266 Updater class
    Copyright (c) 2022 Earle F. Philhower, III <earlephilhower@yahoo.com>
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
*/

#include "Updater.h"

extern "C" {
#include "ch32h4_flash.h"
#include "ch32h4_ota.h"
#include "ch32h4_park.h"
}

#include <Updater_Signing.h>
#ifndef ARDUINO_SIGNING
#define ARDUINO_SIGNING 0
#endif

/* From the linker script. The sketch region is what a U_FLASH update
   replaces; the filesystem partition is what a U_FS update writes. */
extern uint8_t _sketch_start;
extern uint8_t _sketch_end;
extern uint8_t _FS_start;
extern uint8_t _FS_end;


#if ARDUINO_SIGNING
extern UpdaterHashClass& updaterSigningHash;
extern UpdaterVerifyClass& updaterSigningVerifier;
#endif

UpdaterClass::UpdaterClass() {
#if ARDUINO_SIGNING
    /* No stack thunk here: that is an ESP8266 device, where BearSSL needs a
       deeper stack than the SDK leaves. This core has one stack per core and
       it is large enough. */
    installSignature(&updaterSigningHash, &updaterSigningVerifier);
#endif
}

UpdaterClass::~UpdaterClass() {
}

UpdaterClass& UpdaterClass::onProgress(THandlerFunction_Progress fn) {
    _progress_callback = fn;
    return *this;
}

void UpdaterClass::_reset() {
    if (_buffer) {
        delete[] _buffer;
    }
    /* The staged image survives a reset only once end() has accepted it --
       commit() runs afterwards, so that the caller can answer the host before
       the part erases itself. Every other path through here, a failed MD5 or
       a truncated download or an abort, frees it: a retry has to be able to
       allocate a second one, and these are hundreds of kilobytes. */
    if (_staging && !_staged) {
        free(_staging);
        _staging = nullptr;
        _image = nullptr;
        _imageLen = 0;
    }
    _buffer = 0;
    _bufferLen = 0;
    _startAddress = 0;
    _currentAddress = 0;
    _size = 0;
    _command = U_FLASH;
}

bool UpdaterClass::begin(size_t size, int command) {
    uint32_t updateStartAddress;
    if (_size > 0) {
#ifdef DEBUG_UPDATER
        DEBUG_UPDATER.println(F("[begin] already running"));
#endif
        return false;
    }

#ifdef DEBUG_UPDATER
    if (command == U_FS) {
        DEBUG_UPDATER.println(F("[begin] Update Filesystem."));
    }
#endif

    if (size == 0) {
        _setError(UPDATE_ERROR_SIZE);
        return false;
    }

    /* Any previously staged image is stale now. */
    _staged = false;
    _reset();
    clearError(); //  _error = 0
    _target_md5 = "";
    _md5 = MD5Builder();

    if (command == U_FLASH) {
        // Basic sanity: it has to fit in the region it will replace
        if (size > (size_t)(&_sketch_end - &_sketch_start)) {
            _setError(UPDATE_ERROR_SPACE);
            return false;
        }
        /* Staged in RAM. See the note at the top of Updater.h: there is no
           second flash slot to stage into, so the whole image is held in the
           heap until it has been received and verified. That is what caps an
           over-the-air image at what the heap can spare, well below what a
           probe can flash.

           Rounded up to a whole erase page here, once, rather than reallocated
           at commit() time: the committer works in whole pages, and finding
           out that the last few hundred bytes cannot be allocated after the
           image has been received and verified would be a bad place to fail.
           Plus four, so the image can be word-aligned inside it. */
        const uint32_t pageSize = ch32h4_flash_page_size();
        const size_t staged = ((size + pageSize - 1u) & ~(size_t)(pageSize - 1u)) + 4u;
        _staging = (uint8_t *)malloc(staged);
        if (!_staging) {
#ifdef DEBUG_UPDATER
            DEBUG_UPDATER.println(F("[begin] unable to allocate staging buffer"));
#endif
            _setError(UPDATE_ERROR_SPACE);
            return false;
        }
        /* Word-aligned, because the committer copies words. */
        _image = (uint8_t *)(((uintptr_t)_staging + 3u) & ~(uintptr_t)3u);
        _imageLen = size;
        /* The pad past the end of the image is programmed along with it, so
           it has to be something; erased flash is the least surprising. */
        memset(_image + size, 0xFF, staged - 4u - size);
        updateStartAddress = 0;  // Not used
    } else if (command == U_FS) {
        if (&_FS_start + size > &_FS_end) {
            _setError(UPDATE_ERROR_SPACE);
            return false;
        }

        updateStartAddress = (uint32_t)&_FS_start;
    } else {
        // unknown command
#ifdef DEBUG_UPDATER
        DEBUG_UPDATER.println(F("[begin] Unknown update command."));
#endif
        return false;
    }

    //initialize
    _startAddress = updateStartAddress;
    _currentAddress = _startAddress; // Only used in the FS upload case
    _size = size;
    _bufferSize = 4096;
    _buffer = new uint8_t[_bufferSize];
    _command = command;

#ifdef DEBUG_UPDATER
    DEBUG_UPDATER.printf_P(PSTR("[begin] _startAddress:     0x%08lX (%lu)\n"), _startAddress, _startAddress);
    DEBUG_UPDATER.printf_P(PSTR("[begin] _size:             0x%08zX (%zd)\n"), _size, _size);
#endif

    if (!_verify) {
        _md5.begin();
    }
    return true;
}

bool UpdaterClass::setMD5(const char * expected_md5) {
    if (strlen(expected_md5) != 32) {
        return false;
    }
    _target_md5 = expected_md5;
    return true;
}

bool UpdaterClass::end(bool evenIfRemaining) {
    if (_size == 0) {
#ifdef DEBUG_UPDATER
        DEBUG_UPDATER.println(F("no update"));
#endif
        _reset();
        return false;
    }

    // Updating w/o any data is an error we detect here
    if (!progress()) {
        _setError(UPDATE_ERROR_NO_DATA);
    }

    if (hasError() || (!isFinished() && !evenIfRemaining)) {
#ifdef DEBUG_UPDATER
        DEBUG_UPDATER.printf_P(PSTR("premature end: res:%u, pos:%zu/%zu\n"), getError(), progress(), _size);
#endif
        _reset();
        return false;
    }

    if (evenIfRemaining) {
        if (_bufferLen > 0) {
            _writeBuffer();
        }
        _size = progress();
    }

    if (_verify && (_command == U_FLASH)) {
        const uint32_t expectedSigLen = _verify->length();
        // If expectedSigLen is non-zero, we expect the last four bytes of the buffer to
        // contain a matching length field, preceded by the bytes of the signature itself.
        // But if expectedSigLen is zero, we expect neither a signature nor a length field;
        uint32_t sigLen = 0;

        if (expectedSigLen > 0) {
            memcpy(&sigLen, _image + _size - sizeof(uint32_t), sizeof(uint32_t));
        }
#ifdef DEBUG_UPDATER
        DEBUG_UPDATER.printf_P(PSTR("[Updater] sigLen: %lu\n"), sigLen);
#endif
        if (sigLen != expectedSigLen) {
            _setError(UPDATE_ERROR_SIGN);
            _reset();
            return false;
        }

        int binSize = _size;
        if (expectedSigLen > 0) {
            binSize -= (sigLen + sizeof(uint32_t) /* The siglen word */);
        }
        _hash->begin();
#ifdef DEBUG_UPDATER
        DEBUG_UPDATER.printf_P(PSTR("[Updater] Adjusted binsize: %d\n"), binSize);
#endif
        // Calculate the MD5 and hash using proper size
        for (int i = 0; i < binSize; i += 128) {
            size_t read = std::min(128, binSize - i);
            _hash->add(_image + i, read);
        }
        _hash->end();
#ifdef DEBUG_UPDATER
        unsigned char *ret = (unsigned char *)_hash->hash();
        DEBUG_UPDATER.printf_P(PSTR("[Updater] Computed Hash:"));
        for (int i = 0; i < _hash->len(); i++) {
            DEBUG_UPDATER.printf(" %02x", ret[i]);
        }
        DEBUG_UPDATER.printf("\n");
#endif

        uint8_t *sig = nullptr; // Safe to free if we don't actually malloc
        if (expectedSigLen > 0) {
            sig = (uint8_t*)malloc(sigLen);
            if (!sig) {
                _setError(UPDATE_ERROR_SIGN);
                _reset();
                return false;
            }
            memcpy(sig, _image + binSize, sigLen);
#ifdef DEBUG_UPDATER
            DEBUG_UPDATER.printf_P(PSTR("[Updater] Received Signature:"));
            for (size_t i = 0; i < sigLen; i++) {
                DEBUG_UPDATER.printf(" %02x", sig[i]);
            }
            DEBUG_UPDATER.printf("\n");
#endif
        }
        if (!_verify->verify(_hash, (void *)sig, sigLen)) {
            free(sig);
            _setError(UPDATE_ERROR_SIGN);
            _reset();
            return false;
        }
        free(sig);
        _size = binSize; // Adjust size to remove signature, not part of bin payload
        _imageLen = _size; // and it is not part of what gets committed either

#ifdef DEBUG_UPDATER
        DEBUG_UPDATER.printf_P(PSTR("[Updater] Signature matches\n"));
#endif
    } else if (_target_md5.length()) {
        _md5.calculate();
        if (strcasecmp(_target_md5.c_str(), _md5.toString().c_str())) {
            _setError(UPDATE_ERROR_MD5);
            return false;
        }
#ifdef DEBUG_UPDATER
        else {
            DEBUG_UPDATER.printf_P(PSTR("MD5 Success: %s\n"), _target_md5.c_str());
        }
#endif
    }

    if (!_verifyEnd()) {
        _reset();
        return false;
    }

    if (_command == U_FLASH) {
        /* Where arduino-pico hands the file to its bootloader. Here the image
           is already in RAM and verified; commit() puts it in flash, and the
           caller makes that call so that it can answer the host first. */
        _imageLen = _size;
        _staged = true;
#ifdef DEBUG_UPDATER
        DEBUG_UPDATER.printf_P(PSTR("Staged in RAM: %p, size:0x%08zX\n"), _image, _size);
#endif
    }

    _reset();
    return true;
}

bool UpdaterClass::_writeBuffer() {
    if (_command == U_FLASH) {
        /* Into the staging buffer; nothing reaches flash until commit(). For
           U_FLASH the start address is zero, so _currentAddress is already the
           offset into the image. */
        memcpy(_image + _currentAddress, _buffer, _bufferLen);
    } else {
        /* The filesystem partition, which is not the code we are running from,
           so the ordinary driver will do -- and it parks the other core and
           runs the sequences from ITCM itself. */
        const uint32_t page = ch32h4_flash_page_size();
        if ((_currentAddress % page) == 0u) {
            if (!ch32h4_flash_erase(_currentAddress, page)) {
                _setError(UPDATE_ERROR_ERASE);
                return false;
            }
        }
        if (!ch32h4_flash_write(_currentAddress, _buffer, _bufferLen)) {
            _setError(UPDATE_ERROR_WRITE);
            return false;
        }
    }
    if (!_verify) {
        _md5.add(_buffer, _bufferLen);
    }
    _currentAddress += _bufferLen;
    _bufferLen = 0;
    return true;
}

size_t UpdaterClass::maxImageSize() {
    /* The largest block the heap will currently hand out, found by halving.
       There is no malloc_max_block() here, and asking the arena's free total
       would overstate it: what matters is one contiguous allocation.

       Rounded down to 4 KB, and probed with a real malloc/free, because an
       allocator that would fail is the only thing worth reporting. */
    size_t hi = (size_t)(&_sketch_end - &_sketch_start);
    size_t lo = 0;
    while (hi - lo > 4096) {
        size_t mid = lo + (hi - lo) / 2;
        void *p = malloc(mid);
        if (p) {
            free(p);
            lo = mid;
        } else {
            hi = mid;
        }
    }
    /* Leave room for two things: the erase page begin() rounds the buffer up
       to, and enough heap for the sketch to answer the host and run its
       callbacks afterwards. An update that succeeds and then cannot allocate
       a String to reply with is no use. */
    const size_t reserve = ch32h4_flash_page_size() + 8192u;
    return lo > reserve ? lo - reserve : 0;
}

bool UpdaterClass::commit() {
    if (!_staged || !_image || !_imageLen) {
        return false;
    }

    /* Whole erase pages: the committer works in pages, and the tail of the
       last one is erased either way. */
    const uint32_t page = ch32h4_flash_page_size();
    uint32_t len = ((uint32_t)_imageLen + page - 1u) & ~(page - 1u);
    if (len > (uint32_t)(&_sketch_end - &_sketch_start)) {
        return false;
    }
    /* No reallocation here: begin() already rounded the buffer up to a whole
       page and filled the pad. Nothing may fail between the verify and the
       erase. */

    /* The other core has to be out of flash before a page program will
       complete -- see ch32h4_park.c. The committer cannot arrange this
       itself: by the time it runs there is no code left to arrange it with. */
    if (!ch32h4_park_other(200)) {
        return false;
    }

    /* DOES NOT RETURN. It erases the flash it was loaded from, runs out of
       ITCM while doing it, and resets the part when it is done. */
    ch32h4_ota_commit((uint32_t)(uintptr_t)&_sketch_start, _image, len, page);
    return true; // not reached
}

size_t UpdaterClass::write(uint8_t *data, size_t len) {
    if (hasError() || !isRunning()) {
        return 0;
    }

    if (progress() + _bufferLen + len > _size) {
        _setError(UPDATE_ERROR_SPACE);
        return 0;
    }

    size_t left = len;

    while ((_bufferLen + left) > _bufferSize) {
        size_t toBuff = _bufferSize - _bufferLen;
        memcpy(_buffer + _bufferLen, data + (len - left), toBuff);
        _bufferLen += toBuff;
        if (!_writeBuffer()) {
            return len - left;
        }
        left -= toBuff;
        if (!_async) {
            yield();
        }
    }
    //lets see what's left
    memcpy(_buffer + _bufferLen, data + (len - left), left);
    _bufferLen += left;
    if (_bufferLen == remaining()) {
        //we are at the end of the update, so should write what's left to flash
        if (!_writeBuffer()) {
            return len - left;
        }
    }
    return len;
}

bool UpdaterClass::_verifyHeader(uint8_t data) {
    (void) data;
    // No special header on RP2040
    return true;
}

bool UpdaterClass::_verifyEnd() {
    if (_command != U_FLASH) {
        return true;
    }

    /* IS THIS ACTUALLY A SKETCH IMAGE FOR THIS PART?
     *
     * Worth asking, because the answer arrives one instruction before the
     * point of no return. Committing the wrong file erases the running sketch
     * and writes rubbish over it, and the board then needs a probe and a
     * `wlink erase` -- there is no second slot to fall back to.
     *
     * The mistake this actually catches is uploading the whole firmware.bin
     * instead of the OTA image. The full binary starts at the V3F stub and
     * carries the sketch 32 KB further in, so committing it puts the stub
     * where the sketch belongs and the part boots into nothing.
     *
     * The check: at offset 8 of a sketch image is the first word of the V5F
     * vector table, which is the V5F entry address. It is the same value for
     * every sketch built for this variant, so the running sketch is the
     * reference -- no constant to keep in step with the linker script. In a
     * full firmware.bin that offset is padding, and reads zero. */
    if (_size < 12) {
        _setError(UPDATE_ERROR_MAGIC_BYTE);
        return false;
    }
    const uint32_t expect = ((const uint32_t *)(const void *)&_sketch_start)[2];
    uint32_t got;
    memcpy(&got, _image + 8, sizeof(got));
    if (got != expect) {
#ifdef DEBUG_UPDATER
        DEBUG_UPDATER.printf_P(PSTR("[Updater] not a sketch image: entry word 0x%08lX, expected 0x%08lX\n"),
                               (unsigned long)got, (unsigned long)expect);
#endif
        _setError(UPDATE_ERROR_MAGIC_BYTE);
        return false;
    }
    return true;
}

size_t UpdaterClass::writeStream(Stream &data, uint16_t streamTimeout) {
    size_t written = 0;
    size_t toRead = 0;
    if (hasError() || !isRunning()) {
        return 0;
    }

    if (!_verifyHeader(data.peek())) {
#ifdef DEBUG_UPDATER
        printError(DEBUG_UPDATER);
#endif
        _reset();
        return 0;
    }
    /* Upstream uses esp8266::polledTimeout, which this core does not have.
       Same one-shot, spelled out. */
    uint32_t timeOutStart = millis();
    if (_progress_callback) {
        _progress_callback(0, _size);
    }

    while (remaining()) {
        size_t bytesToRead = _bufferSize - _bufferLen;
        if (bytesToRead > remaining()) {
            bytesToRead = remaining();
        }
        toRead = data.readBytes(_buffer + _bufferLen,  bytesToRead);
        if (toRead == 0) { //Timeout
            if (millis() - timeOutStart > streamTimeout) {
                _currentAddress = (_startAddress + _size);
                _setError(UPDATE_ERROR_STREAM);
                _reset();
                return written;
            }
            delay(100);
        } else {
            timeOutStart = millis();
        }
        _bufferLen += toRead;
        if ((_bufferLen == remaining() || _bufferLen == _bufferSize) && !_writeBuffer()) {
            return written;
        }
        written += toRead;
        if (_progress_callback) {
            _progress_callback(progress(), _size);
        }
        yield();
    }
    if (_progress_callback) {
        _progress_callback(progress(), _size);
    }
    return written;
}

void UpdaterClass::_setError(int error) {
    _error = error;
#ifdef DEBUG_UPDATER
    printError(DEBUG_UPDATER);
#endif
    _reset(); // Any error condition invalidates the entire update, so clear partial status
}

void UpdaterClass::printError(Print &out) {
    String err;
    err = "ERROR[";
    err += _error;
    err += "]: ";
    if (_error == UPDATE_ERROR_OK) {
        err += "No Error";
    } else if (_error == UPDATE_ERROR_WRITE) {
        err += "Flash Write Failed";
    } else if (_error == UPDATE_ERROR_ERASE) {
        err += "Flash Erase Failed";
    } else if (_error == UPDATE_ERROR_READ) {
        err += "Flash Read Failed";
    } else if (_error == UPDATE_ERROR_SPACE) {
        err += "Not Enough Space";
    } else if (_error == UPDATE_ERROR_SIZE) {
        err += "Bad Size Given";
    } else if (_error == UPDATE_ERROR_STREAM) {
        err += "Stream Read Timeout";
    } else if (_error == UPDATE_ERROR_NO_DATA) {
        err += "No data supplied";
    } else if (_error == UPDATE_ERROR_MD5) {
        err += "MD5 Failed: expected:";
        err += _target_md5.c_str();
        err += " calculated:";
        err += _md5.toString();
    } else if (_error == UPDATE_ERROR_SIGN) {
        err += "Signature verification failed";
    } else if (_error == UPDATE_ERROR_MAGIC_BYTE) {
        err += "Not a sketch image for this board (upload the OTA binary, "
               "not the full firmware.bin)";
    } else {
        err += "UNKNOWN";
    }
    out.println(err.c_str());
}

UpdaterClass Update;
