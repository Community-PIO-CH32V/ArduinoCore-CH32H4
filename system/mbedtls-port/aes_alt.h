#ifndef AES_ALT_H
#define AES_ALT_H

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include <ch32h417_ecdc.h>

typedef struct {
    ECDC_KEY_TypeDef key;
    ECDC_IV_TypeDef iv;      // optional, used for CBC/CTR
    unsigned int keybits;    // store key size: 128, 192, or 256
} mbedtls_aes_context;

#if defined(MBEDTLS_CIPHER_MODE_XTS)
typedef struct mbedtls_aes_xts_context {
    int dummy;
} mbedtls_aes_xts_context;
#endif

#ifdef __cplusplus
}
#endif

#endif /* aes_alt.h */
