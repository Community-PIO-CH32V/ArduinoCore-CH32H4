#include "MP3Player.h"

#include <string.h>

void MP3Player::setVolume(float v) {
    if (v < 0.0f) { v = 0.0f; }
    if (v > 1.0f) { v = 1.0f; }
    _volQ8 = (uint16_t)(v * 256.0f + 0.5f);
}

void MP3Player::setVolumePercent(uint8_t percent) {
    if (percent > 100) { percent = 100; }
    _volQ8 = (uint16_t)((uint32_t)percent * 256u / 100u);
}

bool MP3Player::begin(Stream &source, AudioSink &sink) {
    if (_running) { return false; }
    if (!_decoder.begin()) { return false; }
    _src = &source;
    _sink = &sink;
    _fill = 0;
    _rateChanges = 0;
    _startedRate = 0;
    _primed = false;
    _running = true;
    return true;
}

void MP3Player::end() {
    if (_sink && _sink->running()) { _sink->end(); }
    _decoder.end();
    _running = false;
    _src = nullptr;
    _sink = nullptr;
    /* The outstanding frame goes with the sink it was meant for: leaving it
       would write a stale frame into whatever sink begin() is given next. */
    _pend = nullptr;
    _pendFrames = 0;
    _pendDone = 0;
}

/* Top the ring up.
 *
 * The ring is a front-aligned buffer rather than a circular one: the decoder
 * needs a contiguous run to find a sync word in, and a memmove of at most
 * 32 KB is cheap next to decoding a frame. */
size_t MP3Player::fillRing() {
    size_t room = RING_BYTES - _fill;
    size_t got = 0;
    while (room) {
        const int avail = _src->available();
        if (avail <= 0) { break; }
        size_t want = (size_t)avail < room ? (size_t)avail : room;
        const int n = _src->readBytes(_ring + _fill, want);
        if (n <= 0) { break; }
        _fill += (size_t)n;
        room -= (size_t)n;
        got += (size_t)n;
    }
    return got;
}

bool MP3Player::decodeOne() {
    size_t used = 0;
    const int samples = _decoder.decodeFrame(_ring, _fill, &used,
                                             _pcm, MP3Decoder::MAX_SAMPLES);
    if (used) {
        memmove(_ring, _ring + used, _fill - used);
        _fill -= used;
    }
    if (samples <= 0) { return false; }

    /* The rate is not known until a frame decodes, so the sink starts here
       rather than in begin(). A mid-stream rate change restarts it, because
       playing on at the wrong speed is audible and invisible in the logs. */
    const uint32_t rate = _decoder.sampleRate();
    if (!_sink->running()) {
        if (!_sink->begin(rate)) { return false; }
        _startedRate = rate;
    } else if (rate != _startedRate) {
        _sink->end();
        if (!_sink->begin(rate)) { return false; }
        _startedRate = rate;
        _rateChanges++;
    }

    /* AudioSink takes interleaved stereo. Mono is duplicated rather than
       written at half length, which would come out at double speed. */
    const size_t frames = (_decoder.channels() == 2)
                        ? (size_t)samples / 2u : (size_t)samples;
    const int16_t *out = _pcm;
    if (_decoder.channels() == 1) {
        for (size_t i = 0; i < frames; i++) {
            _stereo[i * 2] = _pcm[i];
            _stereo[i * 2 + 1] = _pcm[i];
        }
        out = _stereo;
    }

    if (_volQ8 != 256) {
        /* In place into _stereo either way, so a mono source is scaled once
           rather than twice and the decoder's own buffer stays untouched. */
        if (out != _stereo) {
            for (size_t i = 0; i < frames * 2u; i++) { _stereo[i] = out[i]; }
            out = _stereo;
        }
        for (size_t i = 0; i < frames * 2u; i++) {
            _stereo[i] = (int16_t)(((int32_t)_stereo[i] * _volQ8) >> 8);
        }
    }

    /* Handed over as a whole, and written across as many loop() calls as the
       sink needs. What used to be here wrote whatever fitted and dropped the
       remainder, which loses audio rather than delaying it. */
    _pend = out;
    _pendFrames = frames;
    _pendDone = 0;
    drainPending();
    return true;
}

void MP3Player::drainPending() {
    while (_pendDone < _pendFrames) {
        const size_t n = _sink->writeFrames(_pend + _pendDone * 2,
                                            _pendFrames - _pendDone);
        if (n == 0) { break; }          /* sink full; the rest waits */
        _pendDone += n;
    }
    if (_pendDone >= _pendFrames) {
        _pend = nullptr;
        _pendFrames = 0;
        _pendDone = 0;
    }
}

bool MP3Player::loop() {
    if (!_running) { return false; }

    fillRing();

    if (!_primed) {
        const bool sourceDone = _src->available() <= 0;
        if (_fill >= PRIME_BYTES || (sourceDone && _fill > 0)) {
            _primed = true;
        } else {
            return true;          /* still filling; not an error */
        }
    }

    /* WHAT IS ALREADY DECODED GOES FIRST, and nothing new is decoded until it
     * has all been handed over.
     *
     * This used to ask the sink for room for a WHOLE frame -- 1152 frames --
     * before decoding, which deadlocked against any sink whose buffer is
     * smaller than that. The I2S sink's default ring is 4096 bytes, so
     * availableFrames() maxes out at 1023 for 16-bit stereo and the condition
     * could never be met: the first frame started the sink, nothing decoded
     * again, and the output starved. It reported as a rising underrun count
     * with a completely full input ring, which reads like a slow network and
     * is the opposite.
     *
     * There is no busy-spin. When the sink is full the drain moves nothing and
     * this returns, and at most one frame is ever outstanding. */
    if (_pendFrames) {
        drainPending();
        if (_pendFrames) {
            return true;          /* sink still full; try again next call */
        }
    }

    if (_fill == 0) {
        _running = _src->available() > 0;
        return _running;
    }
    decodeOne();
    return true;
}
