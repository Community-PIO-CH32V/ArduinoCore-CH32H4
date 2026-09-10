#include "MP3Player.h"

#include <string.h>

void MP3Player::setVolume(float v) {
    if (v < 0.0f) { v = 0.0f; }
    if (v > 1.0f) { v = 1.0f; }
    _volQ8 = (uint16_t)(v * 256.0f + 0.5f);
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

    size_t done = 0;
    while (done < frames) {
        const size_t n = _sink->writeFrames(out + done * 2, frames - done);
        if (n == 0) { break; }    /* full; the rest waits for the next loop */
        done += n;
    }
    return true;
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

    if (_sink->running() && _sink->availableFrames() < 1152) {
        return true;              /* no room for a frame yet */
    }

    if (_fill == 0) {
        _running = _src->available() > 0;
        return _running;
    }
    decodeOne();
    return true;
}
