#include "mbedtls/aes.h"
#include "mbedtls/platform_util.h"
#include "mbedtls/error.h"
#ifdef MBEDTLS_AES_ALT
#include "ch32h417_ecdc.h"
#include "ch32h417.h"
#include <string.h>
#include <stdbool.h>
#include <stdio.h>

/* Which ECDC ExcuteMode value really means "encrypt".
 *
 * openwch/ch32h417 issue #10 reports ECDC_SingleTime_Encrypt (0x2) and
 * ECDC_SingleTime_Decrypt (0xa) swapped in the vendor header, so the header is
 * not trustworthy on this point and neither is any argument from it. The
 * mapping below is the one that reproduces the FIPS-197 AES-128 ECB known
 * answer on this silicon:
 *
 *     key        000102030405060708090a0b0c0d0e0f
 *     plaintext  00112233445566778899aabbccddeeff
 *     ciphertext 69c4e0d86a7b0430d8cdb78070b4c55a
 *
 * ports/ch32/test_mbedtls.py checks exactly that, so if a future SDK drop
 * fixes the header the test says so immediately rather than TLS quietly
 * failing to interoperate.
 *
 * Swapping these two lines is the entire fix if that day comes. */
#define CH32_ECDC_ENCRYPT (ECDC_SingleTime_Encrypt)
#define CH32_ECDC_DECRYPT (ECDC_SingleTime_Decrypt)

static bool aes_inited = false;

/* The vendor file this came from traced heavily through printf. The call
 * sites are kept, because they document the flow better than a comment would,
 * but they compile to nothing: this runs once per AES block, and stdio inside
 * TLS would be both ruinous and re-entrant.
 *
 * A named macro rather than #define printf(...) -- redefining printf for a
 * whole translation unit makes any other use of it silently disappear. */
#ifdef CH32H4_AES_TRACE
#include <stdio.h>
#define AES_TRACE(...)  printf(__VA_ARGS__)
#else
#define AES_TRACE(...)  do { } while (0)
#endif

typedef uint16_t /* __packed*/ mbedtls_uint16_unaligned_t;
typedef uint32_t /*__packed*/ mbedtls_uint32_unaligned_t;
typedef uint64_t /*__packed*/ mbedtls_uint64_unaligned_t;

__attribute__((always_inline)) static inline uint32_t mbedtls_get_unaligned_uint32(const void *p) {
    uint32_t r;
    mbedtls_uint32_unaligned_t *p32 = (mbedtls_uint32_unaligned_t *)p;
    r = *p32;
    return r;
}

__attribute__((always_inline)) static inline void mbedtls_put_unaligned_uint32(void *p, uint32_t x) {
    mbedtls_uint32_unaligned_t *p32 = (mbedtls_uint32_unaligned_t *)p;
    *p32 = x;
}

__attribute__((always_inline)) static inline void mbedtls_xor_no_simd(unsigned char *r,
    const unsigned char *a,
    const unsigned char *b,
    size_t n) {
    size_t i = 0;
    for (; (i + 4) <= n; i += 4)
    {
        uint32_t x = mbedtls_get_unaligned_uint32(a + i) ^ mbedtls_get_unaligned_uint32(b + i);
        mbedtls_put_unaligned_uint32(r + i, x);
    }
}

static void aes_hw_init(void) {
    if (aes_inited) {
        AES_TRACE("[AES_HW] Already initialized\n");
        return;
    }

    AES_TRACE("[AES_HW] Initializing hardware...\n");
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_ECDC, ENABLE);
    ECDC_HardwareClockCmd(ENABLE);
    ECDC_ClockConfig(ECDC_ClockSource_PLLCLK_Div1);
    ECDC_ClearFlag(ECDC_FLAG_Single_END);
    aes_inited = true;
    AES_TRACE("[AES_HW] Hardware initialized\n");
}

static void print_hex(const char *label, const uint8_t *data, size_t len) {
    /* AES_TRACE is empty unless tracing is compiled in, and then nothing reads
       these. Cast rather than drop the parameters: the signature is what makes
       the call sites readable. */
    (void)label;
    (void)data;
    AES_TRACE("%s: ", label);
    for (size_t i = 0; i < len; i++)
    {
        AES_TRACE("%02x", data[i]);
    }
    AES_TRACE("\n");
}

static void print_words(const char *label, const uint32_t *words, size_t count) {
    (void)label;
    (void)words;
    AES_TRACE("%s: ", label);
    for (size_t i = 0; i < count; i++)
    {
        AES_TRACE("0x%08x ", words[i]);
    }
    AES_TRACE("\n");
}

/**
 * @brief Fill ECDC_KEY_TypeDef structure from a byte array key
 * @param key_struct Pointer to ECDC_KEY_TypeDef to fill
 * @param key_bytes Pointer to key bytes (16/24/32 bytes for AES-128/192/256)
 * @param key_len Length in bits (128, 192, or 256)
 * @note This function handles big-endian word ordering for ECDCExcuteEndian_Big
 */
static void ECDC_FillKey_BigEndian(ECDC_KEY_TypeDef *key_struct, const uint8_t *key_bytes, uint16_t key_len) {
    /* The key sits RIGHT-ALIGNED in the 256-bit register file with its first
     * byte at the most significant end of the key itself -- so a 128-bit key
     * occupies KEY_127T96..KEY_31T0, and a 256-bit key occupies
     * KEY_255T224..KEY_31T0.
     *
     * Written generically rather than as one branch per size: the original
     * per-size version placed 128-bit keys correctly and left longer keys with
     * their first word still at KEY_127T96, so AES-256 encrypted under a key
     * the hardware had assembled differently from the one asked for. Every
     * FIPS-197 AES-256 vector failed while AES-128 passed. */
    uint32_t w[8] = { 0 };
    unsigned nwords = key_len / 32;
    if (nwords > 8) {
        nwords = 8;
    }

    for (unsigned i = 0; i < nwords; i++) {
        uint32_t v = ((uint32_t)key_bytes[4 * i] << 24) |
            ((uint32_t)key_bytes[4 * i + 1] << 16) |
            ((uint32_t)key_bytes[4 * i + 2] << 8) |
            ((uint32_t)key_bytes[4 * i + 3]);
        // Key word 0 is the most significant word of the key.
        w[nwords - 1 - i] = v;
    }

    key_struct->KEY_31T0 = w[0];
    key_struct->KEY_63T32 = w[1];
    key_struct->KEY_95T64 = w[2];
    key_struct->KEY_127T96 = w[3];
    key_struct->KEY_159T128 = w[4];
    key_struct->KEY_191T160 = w[5];
    key_struct->KEY_223T192 = w[6];
    key_struct->KEY_255T224 = w[7];
}

/**
 * @brief Convert standard byte-oriented input to hardware word format
 * @param output 4-word output array (hardware format, reversed word order)
 * @param input 16-byte input array (standard format)
 */
static void bytes_to_hw_words(uint32_t output[4], const uint8_t input[16]) {
    AES_TRACE("[BYTES_TO_HW] Input bytes:\n");
    print_hex("[BYTES_TO_HW]   ", input, 16);

    // Hardware expects words in reverse order with big-endian bytes
    // Standard: byte[0..3] byte[4..7] byte[8..11] byte[12..15]
    // Hardware: word[3] word[2] word[1] word[0]

    output[3] = ((uint32_t)input[0] << 24) |
        ((uint32_t)input[1] << 16) |
        ((uint32_t)input[2] << 8) |
        ((uint32_t)input[3]);

    output[2] = ((uint32_t)input[4] << 24) |
        ((uint32_t)input[5] << 16) |
        ((uint32_t)input[6] << 8) |
        ((uint32_t)input[7]);

    output[1] = ((uint32_t)input[8] << 24) |
        ((uint32_t)input[9] << 16) |
        ((uint32_t)input[10] << 8) |
        ((uint32_t)input[11]);

    output[0] = ((uint32_t)input[12] << 24) |
        ((uint32_t)input[13] << 16) |
        ((uint32_t)input[14] << 8) |
        ((uint32_t)input[15]);

    print_words("[BYTES_TO_HW] Output words", output, 4);
}

/**
 * @brief Convert hardware word format to standard byte-oriented output
 * @param output 16-byte output array (standard format)
 * @param input 4-word input array (hardware format, reversed word order)
 */
static void hw_words_to_bytes(uint8_t output[16], const uint32_t input[4]) {
    print_words("[HW_TO_BYTES] Input words", input, 4);

    // Hardware returns words in reverse order with big-endian bytes
    // Hardware: word[3] word[2] word[1] word[0]
    // Standard: byte[0..3] byte[4..7] byte[8..11] byte[12..15]

    output[0] = (input[3] >> 24) & 0xFF;
    output[1] = (input[3] >> 16) & 0xFF;
    output[2] = (input[3] >> 8) & 0xFF;
    output[3] = input[3] & 0xFF;

    output[4] = (input[2] >> 24) & 0xFF;
    output[5] = (input[2] >> 16) & 0xFF;
    output[6] = (input[2] >> 8) & 0xFF;
    output[7] = input[2] & 0xFF;

    output[8] = (input[1] >> 24) & 0xFF;
    output[9] = (input[1] >> 16) & 0xFF;
    output[10] = (input[1] >> 8) & 0xFF;
    output[11] = input[1] & 0xFF;

    output[12] = (input[0] >> 24) & 0xFF;
    output[13] = (input[0] >> 16) & 0xFF;
    output[14] = (input[0] >> 8) & 0xFF;
    output[15] = input[0] & 0xFF;

    print_hex("[HW_TO_BYTES] Output bytes", output, 16);
}

void mbedtls_aes_init(mbedtls_aes_context *ctx) {
    AES_TRACE("[AES_INIT] Initializing context at %p\n", (void *)ctx);
    memset(ctx, 0, sizeof(mbedtls_aes_context));
    aes_hw_init();
}

void mbedtls_aes_free(mbedtls_aes_context *ctx) {
    AES_TRACE("[AES_FREE] Freeing context at %p\n", (void *)ctx);
    if (ctx != NULL) {
        memset(ctx, 0, sizeof(mbedtls_aes_context));
    }
}

int mbedtls_aes_setkey_enc(mbedtls_aes_context *ctx,
    const unsigned char *key,
    unsigned int keybits) {
    AES_TRACE("[SETKEY_ENC] Setting encryption key, %d bits\n", keybits);
    print_hex("[SETKEY_ENC] Key", key, keybits / 8);

    if (keybits != 128 && keybits != 192 && keybits != 256) {
        AES_TRACE("[SETKEY_ENC] ERROR: Invalid key length %d\n", keybits);
        return MBEDTLS_ERR_AES_INVALID_KEY_LENGTH;
    }

    ctx->keybits = keybits;
    ECDC_FillKey_BigEndian(&ctx->key, key, keybits);
    AES_TRACE("[SETKEY_ENC] Key set successfully\n");
    return 0;
}

#if !defined(MBEDTLS_BLOCK_CIPHER_NO_DECRYPT)
int mbedtls_aes_setkey_dec(mbedtls_aes_context *ctx,
    const unsigned char *key,
    unsigned int keybits) {
    AES_TRACE("[SETKEY_DEC] Setting decryption key, %d bits\n", keybits);
    return mbedtls_aes_setkey_enc(ctx, key, keybits);
}
#endif

static ECDC_KeyLen_TypeDef get_key_len_enum(unsigned int keybits) {
    ECDC_KeyLen_TypeDef result;
    switch (keybits)
    {
        case 128:
            result = ECDCKeyLen_128b;
            break;
        case 192:
            result = ECDCKeyLen_192b;
            break;
        case 256:
            result = ECDCKeyLen_256b;
            break;
        default:
            result = ECDCKeyLen_128b;
            break;
    }
    AES_TRACE("[GET_KEY_LEN] %d bits -> enum value %d\n", keybits, result);
    return result;
}

static void ecdc_setup_single(ECDC_InitTypeDef *cfg, int mode, unsigned int keybits) {
    AES_TRACE("[ECDC_SETUP] Mode: %s, Key: %d bits\n",
        mode == MBEDTLS_AES_ENCRYPT ? "ENCRYPT" : "DECRYPT", keybits);

    cfg->Algorithm = ECDCAlgorithm_AES;
    cfg->BlockCipherMode = ECDCBlockCipherMode_ECB;
    /* CH32_ECDC_ENCRYPT/DECRYPT rather than the SDK's own names: openwch
     * issue #10 reports the two swapped in ch32h417_ecdc.h. Which way round
     * they really go is settled by a known-answer test, not by reading the
     * header -- see the mapping definition in this file. */
    cfg->ExcuteMode = (mode == MBEDTLS_AES_ENCRYPT)
                          ? CH32_ECDC_ENCRYPT
                          : CH32_ECDC_DECRYPT;
    cfg->ExcuteEndian = ECDCExcuteEndian_Big;
    cfg->KeyLen = get_key_len_enum(keybits);

    AES_TRACE("[ECDC_SETUP] Algorithm=%d BlockCipherMode=%d ExcuteMode=%d ExcuteEndian=%d KeyLen=%d\n",
        cfg->Algorithm, cfg->BlockCipherMode, cfg->ExcuteMode,
        cfg->ExcuteEndian, cfg->KeyLen);
}

int mbedtls_aes_crypt_ecb(mbedtls_aes_context *ctx, int mode,
    const unsigned char input[16],
    unsigned char output[16]) {
    // Cache last configuration to avoid reinit on every block
    static int last_mode = -1;
    static unsigned int last_keybits = 0;
    static ECDC_KEY_TypeDef last_key = {0};

    // Check if we need to reconfigure
    if (last_mode != mode || last_keybits != ctx->keybits ||
        memcmp(&last_key, &ctx->key, sizeof(ECDC_KEY_TypeDef)) != 0) {

        ECDC_InitTypeDef cfg = {0};
        ecdc_setup_single(&cfg, mode, ctx->keybits);
        cfg.Key = &ctx->key;
        cfg.IV = NULL;

        ECDC_Init(&cfg);

        last_mode = mode;
        last_keybits = ctx->keybits;
        memcpy(&last_key, &ctx->key, sizeof(ECDC_KEY_TypeDef));
    }

    // Convert input to hardware format
    uint32_t data[4];
    bytes_to_hw_words(data, input);

    // Write data and process
    ECDC_SingleWR_RawData(data);

    // Wait for completion
    while (!ECDC_GetFlagStatus(ECDC_FLAG_Single_END)) {
        ;
    }

    // Clear flag for next operation
    ECDC_ClearFlag(ECDC_FLAG_Single_END);

    // Read result
    uint32_t res[4] = {0};
    ECDC_SingleRD_EcdcData(res);

    // Convert output from hardware format
    hw_words_to_bytes(output, res);

    return 0;
}

int mbedtls_aes_crypt_cbc(mbedtls_aes_context *ctx,
    int mode,
    size_t length,
    unsigned char iv[16],
    const unsigned char *input,
    unsigned char *output) {
    int ret = MBEDTLS_ERR_ERROR_CORRUPTION_DETECTED;
    unsigned char temp[16];

    if (mode != MBEDTLS_AES_ENCRYPT && mode != MBEDTLS_AES_DECRYPT) {
        return MBEDTLS_ERR_AES_BAD_INPUT_DATA;
    }

    /* Nothing to do if length is zero. */
    if (length == 0) {
        return 0;
    }

    if (length % 16) {
        return MBEDTLS_ERR_AES_INVALID_INPUT_LENGTH;
    }

    const unsigned char *ivp = iv;

    if (mode == MBEDTLS_AES_DECRYPT) {
        while (length > 0) {
            memcpy(temp, input, 16);
            ret = mbedtls_aes_crypt_ecb(ctx, mode, input, output);
            if (ret != 0) {
                goto exit;
            }
            /* Avoid using the NEON implementation of mbedtls_xor. Because of the dependency on
             * the result for the next block in CBC, and the cost of transferring that data from
             * NEON registers, NEON is slower on aarch64. */
            mbedtls_xor_no_simd(output, output, iv, 16);

            memcpy(iv, temp, 16);

            input += 16;
            output += 16;
            length -= 16;
        }
    } else {
        while (length > 0) {
            mbedtls_xor_no_simd(output, input, ivp, 16);

            ret = mbedtls_aes_crypt_ecb(ctx, mode, output, output);
            if (ret != 0) {
                goto exit;
            }
            ivp = output;

            input += 16;
            output += 16;
            length -= 16;
        }
        memcpy(iv, ivp, 16);
    }
    ret = 0;

exit:
    return ret;
}

int mbedtls_aes_crypt_ctr(mbedtls_aes_context *ctx, size_t length, size_t *nc_off,
    unsigned char nonce_counter[16],
    unsigned char stream_block[16],
    const unsigned char *input,
    unsigned char *output) {
    AES_TRACE("\n[AES_CTR] === Starting CTR operation ===\n");
    AES_TRACE("[AES_CTR] Length: %zu bytes, Offset: %zu\n", length, *nc_off);
    print_hex("[AES_CTR] Nonce", nonce_counter, 16);

    size_t offset = *nc_off;
    unsigned char block[16];
    size_t byte_count = 0;

    while (length--) {
        if (offset == 0) {
            AES_TRACE("[AES_CTR] Generating new keystream block (byte %zu)\n", byte_count);
            mbedtls_aes_crypt_ecb(ctx, MBEDTLS_AES_ENCRYPT, nonce_counter, block);

            // Increment counter (big-endian)
            for (int i = 15; i >= 0; i--)
            {
                if (++nonce_counter[i] != 0) {
                    break;
                }
            }
            memcpy(stream_block, block, 16);
        }
        *output++ = *input++ ^ stream_block[offset];
        offset = (offset + 1) & 0x0F;
        byte_count++;
    }
    *nc_off = offset;
    AES_TRACE("[AES_CTR] === CTR operation complete ===\n\n");
    return 0;
}
#endif
