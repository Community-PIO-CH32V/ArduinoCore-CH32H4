#include "CATrustStore.h"

#include <string.h>

/* platform.h for mbedtls_calloc/mbedtls_free: the candidate list handed back
   to mbedtls is freed BY mbedtls, so it has to come from the same allocator
   the library itself uses rather than from plain new or malloc. */
#include <mbedtls/platform.h>
#include <mbedtls/ssl.h>
#include <mbedtls/x509_crt.h>

/* The tables. Only the selected one is included, so the other costs nothing:
 * this is why the choice is a compile-time macro rather than a runtime one. */
#if CH32H4_CA_STORE == CH32H4_CA_STORE_MINIMAL
#include "ca_roots_minimal.h"
#elif CH32H4_CA_STORE == CH32H4_CA_STORE_FULL
#include "ca_roots_full.h"
#else
static const ch32h4_ca_entry_t CH32H4_CA_ROOTS[] = { { nullptr, 0, nullptr, 0 } };
static const size_t CH32H4_CA_ROOT_COUNT = 0;
#endif

size_t ch32h4_ca_store_size(void) {
    return CH32H4_CA_ROOT_COUNT;
}

#if CH32H4_CA_STORE != CH32H4_CA_STORE_NONE

/* Hand mbedtls the roots that could have signed this certificate.
 *
 * WHY A CALLBACK AND NOT A CHAIN. mbedtls_ssl_conf_ca_chain() wants every
 * trusted root parsed up front, and a parsed certificate costs more RAM than
 * its DER. For 121 roots that is more than this part has, and all but one of
 * them is wasted on any given handshake.
 *
 * So the store stays in flash as DER and this runs per handshake, comparing
 * the child's issuer_raw against each root's pre-extracted subject with a
 * memcmp -- both are DER-encoded names, which is what mbedtls documents those
 * fields for. Only a root that matches is parsed.
 *
 * Ownership: mbedtls frees the list this returns, including on failure paths,
 * so everything handed back must be heap allocated and fully initialised.
 */
static int ca_lookup(void *ctx, const mbedtls_x509_crt *child,
                     mbedtls_x509_crt **candidates) {
    (void)ctx;
    *candidates = nullptr;
    if (child == nullptr) {
        return 0;
    }

    mbedtls_x509_crt *head = nullptr;
    mbedtls_x509_crt *tail = nullptr;

    for (size_t i = 0; i < CH32H4_CA_ROOT_COUNT; i++) {
        const ch32h4_ca_entry_t *e = &CH32H4_CA_ROOTS[i];
        if (e->subject_len != child->issuer_raw.len ||
                memcmp(e->subject, child->issuer_raw.p, e->subject_len) != 0) {
            continue;
        }

        mbedtls_x509_crt *crt = (mbedtls_x509_crt *)mbedtls_calloc(
            1, sizeof(mbedtls_x509_crt));
        if (crt == nullptr) {
            break;      /* out of memory: verify with what was found so far */
        }
        mbedtls_x509_crt_init(crt);
        if (mbedtls_x509_crt_parse_der(crt, e->der, e->der_len) != 0) {
            /* A root in our own table that will not parse is a generator bug,
               not a server problem. Skip it rather than failing the
               handshake: another root may still match. */
            mbedtls_x509_crt_free(crt);
            mbedtls_free(crt);
            continue;
        }

        if (tail == nullptr) {
            head = crt;
        } else {
            tail->next = crt;
        }
        tail = crt;
    }

    *candidates = head;
    return 0;
}

bool ch32h4_ca_store_install(mbedtls_ssl_config *conf) {
    if (conf == nullptr || CH32H4_CA_ROOT_COUNT == 0) {
        return false;
    }
    mbedtls_ssl_conf_ca_cb(conf, ca_lookup, nullptr);
    return true;
}

#else

bool ch32h4_ca_store_install(mbedtls_ssl_config *conf) {
    (void)conf;
    return false;       /* no store compiled in */
}

#endif
