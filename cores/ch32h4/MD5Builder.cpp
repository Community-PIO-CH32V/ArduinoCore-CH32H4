/* MD5, RFC 1321. See the header for why this is here rather than borrowed
 * from mbedTLS or lwIP.
 *
 * A direct transcription of the algorithm in the RFC: the four rounds, the
 * per-round shift amounts, and the sine-derived constant table. It is written
 * out rather than table-compressed because the compressed form is harder to
 * check against the specification and this is not on any hot path -- a digest
 * authentication happens once per request, over a few dozen bytes.
 */
#include "MD5Builder.h"

#include <string.h>

namespace {

inline uint32_t rotl(uint32_t x, int c) {
    return (x << c) | (x >> (32 - c));
}

/* K[i] = floor(2^32 * abs(sin(i + 1))), the RFC's table. */
const uint32_t K[64] = {
    0xd76aa478u, 0xe8c7b756u, 0x242070dbu, 0xc1bdceeeu,
    0xf57c0fafu, 0x4787c62au, 0xa8304613u, 0xfd469501u,
    0x698098d8u, 0x8b44f7afu, 0xffff5bb1u, 0x895cd7beu,
    0x6b901122u, 0xfd987193u, 0xa679438eu, 0x49b40821u,
    0xf61e2562u, 0xc040b340u, 0x265e5a51u, 0xe9b6c7aau,
    0xd62f105du, 0x02441453u, 0xd8a1e681u, 0xe7d3fbc8u,
    0x21e1cde6u, 0xc33707d6u, 0xf4d50d87u, 0x455a14edu,
    0xa9e3e905u, 0xfcefa3f8u, 0x676f02d9u, 0x8d2a4c8au,
    0xfffa3942u, 0x8771f681u, 0x6d9d6122u, 0xfde5380cu,
    0xa4beea44u, 0x4bdecfa9u, 0xf6bb4b60u, 0xbebfbc70u,
    0x289b7ec6u, 0xeaa127fau, 0xd4ef3085u, 0x04881d05u,
    0xd9d4d039u, 0xe6db99e5u, 0x1fa27cf8u, 0xc4ac5665u,
    0xf4292244u, 0x432aff97u, 0xab9423a7u, 0xfc93a039u,
    0x655b59c3u, 0x8f0ccc92u, 0xffeff47du, 0x85845dd1u,
    0x6fa87e4fu, 0xfe2ce6e0u, 0xa3014314u, 0x4e0811a1u,
    0xf7537e82u, 0xbd3af235u, 0x2ad7d2bbu, 0xeb86d391u,
};

/* Per-round left-rotation amounts. */
const int S[64] = {
    7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,  7, 12, 17, 22,
    5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,  5,  9, 14, 20,
    4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,  4, 11, 16, 23,
    6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,  6, 10, 15, 21,
};

}  // namespace

void MD5Builder::begin() {
    _state[0] = 0x67452301u;
    _state[1] = 0xefcdab89u;
    _state[2] = 0x98badcfeu;
    _state[3] = 0x10325476u;
    _bits = 0;
    _buflen = 0;
    memset(_buf, 0, sizeof(_buf));
    memset(_digest, 0, sizeof(_digest));
}

void MD5Builder::transform(const uint8_t block[64]) {
    /* MD5 is little-endian throughout, and this part is RISC-V, so the words
     * could be read straight out of the block. They are assembled byte by
     * byte anyway: the block comes from a network buffer with no alignment
     * guarantee, and an unaligned 32-bit load is a fault on some of the cores
     * this code might be carried to. */
    uint32_t M[16];
    for (int i = 0; i < 16; i++) {
        M[i] = (uint32_t)block[i * 4]
             | ((uint32_t)block[i * 4 + 1] << 8)
             | ((uint32_t)block[i * 4 + 2] << 16)
             | ((uint32_t)block[i * 4 + 3] << 24);
    }

    uint32_t a = _state[0], b = _state[1], c = _state[2], d = _state[3];

    for (int i = 0; i < 64; i++) {
        uint32_t f;
        int g;
        if (i < 16) {
            f = (b & c) | (~b & d);
            g = i;
        } else if (i < 32) {
            f = (d & b) | (~d & c);
            g = (5 * i + 1) % 16;
        } else if (i < 48) {
            f = b ^ c ^ d;
            g = (3 * i + 5) % 16;
        } else {
            f = c ^ (b | ~d);
            g = (7 * i) % 16;
        }
        const uint32_t tmp = d;
        d = c;
        c = b;
        b = b + rotl(a + f + K[i] + M[g], S[i]);
        a = tmp;
    }

    _state[0] += a;
    _state[1] += b;
    _state[2] += c;
    _state[3] += d;
}

void MD5Builder::add(const uint8_t *data, uint16_t len) {
    if (data == nullptr) {
        return;
    }
    _bits += (uint64_t)len * 8u;

    size_t i = 0;
    while (i < len) {
        const size_t room = 64 - _buflen;
        const size_t take = ((size_t)(len - i) < room) ? (size_t)(len - i) : room;
        memcpy(_buf + _buflen, data + i, take);
        _buflen += take;
        i += take;
        if (_buflen == 64) {
            transform(_buf);
            _buflen = 0;
        }
    }
}

void MD5Builder::calculate() {
    /* The padding: a single 1 bit, zeroes up to 56 mod 64, then the original
     * length in bits as a little-endian 64-bit value. The length is captured
     * before padding is added, since add() would otherwise count it. */
    const uint64_t bits = _bits;

    uint8_t pad = 0x80;
    add(&pad, 1);
    pad = 0x00;
    while (_buflen != 56) {
        add(&pad, 1);
    }

    uint8_t tail[8];
    for (int i = 0; i < 8; i++) {
        tail[i] = (uint8_t)((bits >> (8 * i)) & 0xFF);
    }
    /* Written straight into the buffer rather than through add(), which would
     * add these eight bytes to the length it is encoding. */
    memcpy(_buf + _buflen, tail, 8);
    transform(_buf);
    _buflen = 0;

    for (int i = 0; i < 4; i++) {
        _digest[i * 4]     = (uint8_t)(_state[i] & 0xFF);
        _digest[i * 4 + 1] = (uint8_t)((_state[i] >> 8) & 0xFF);
        _digest[i * 4 + 2] = (uint8_t)((_state[i] >> 16) & 0xFF);
        _digest[i * 4 + 3] = (uint8_t)((_state[i] >> 24) & 0xFF);
    }
}

void MD5Builder::getBytes(uint8_t *out) const {
    if (out) {
        memcpy(out, _digest, sizeof(_digest));
    }
}

arduino::String MD5Builder::toString() const {
    static const char hex[] = "0123456789abcdef";
    char out[33];
    for (int i = 0; i < 16; i++) {
        out[i * 2] = hex[(_digest[i] >> 4) & 0xF];
        out[i * 2 + 1] = hex[_digest[i] & 0xF];
    }
    out[32] = '\0';
    return arduino::String(out);
}
