#include "MP3Decoder.h"

extern "C" {
#include "libhelix-mp3/mp3dec.h"
}

bool MP3Decoder::begin() {
    if (_dec) { return true; }
    _dec = MP3InitDecoder();
    _rate = 0; _bitrate = 0; _frames = 0; _errors = 0; _channels = 0;
    return _dec != nullptr;
}

void MP3Decoder::end() {
    if (_dec) {
        MP3FreeDecoder((HMP3Decoder)_dec);
        _dec = nullptr;
    }
}

int MP3Decoder::decodeFrame(const uint8_t *in, size_t inLen, size_t *consumed,
                            int16_t *pcm, size_t pcmCap) {
    *consumed = 0;
    if (!_dec || !in || inLen == 0 || pcmCap < MAX_SAMPLES) { return 0; }

    /* Find the frame. A radio stream begins at an arbitrary byte, so this is
       the normal path and not just the error path. */
    const int off = MP3FindSyncWord((unsigned char *)in, (int)inLen);
    if (off < 0) {
        /* No sync anywhere. Keep the last two bytes: a sync word may straddle
           the end of what we have been given. */
        *consumed = inLen > 2 ? inLen - 2 : 0;
        return 0;
    }
    *consumed = (size_t)off;

    unsigned char *p = (unsigned char *)in + off;
    int left = (int)(inLen - (size_t)off);
    const int before = left;

    const int rc = MP3Decode((HMP3Decoder)_dec, &p, &left, pcm, 0);
    if (rc == ERR_MP3_INDATA_UNDERFLOW || rc == ERR_MP3_MAINDATA_UNDERFLOW) {
        /* Not an error: the frame is real but incomplete. Consume only the
           skipped bytes so the caller retries once more has arrived. */
        return 0;
    }
    if (rc != ERR_MP3_NONE) {
        /* Step past this sync word so the next search cannot find it again
           and loop forever. */
        *consumed = (size_t)off + 1;
        _errors++;
        return -1;
    }

    *consumed = (size_t)off + (size_t)(before - left);

    MP3FrameInfo info;
    MP3GetLastFrameInfo((HMP3Decoder)_dec, &info);
    _rate = (uint32_t)info.samprate;
    _channels = (uint8_t)info.nChans;
    _bitrate = (uint32_t)info.bitrate;
    _frames++;
    return info.outputSamps;
}
