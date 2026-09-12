/* The built-in CA trust store, and how to choose one.
 *
 * By default this core trusts NOTHING. A TLS client either gets a root from
 * the sketch through setCACert(), or refuses to connect. That is the right
 * default for a microcontroller -- it makes the trust decision visible in the
 * sketch -- but it means every https URL needs a certificate pasted in, and
 * the certificate that a station's CA uses changes without notice.
 *
 * A store can be compiled in instead:
 *
 *     PlatformIO       build_flags = -DCH32H4_CA_STORE=1
 *     Arduino IDE      Tools > CA trust store > Minimal
 *
 *     0  none      nothing trusted unless the sketch says so   (default)
 *     1  minimal   15 roots, about 13 KB of flash
 *     2  full      every Mozilla root, 121 of them, about 126 KB
 *
 * The sizes are the DER in flash. RAM is not proportional: the store is read
 * by a callback that parses only the root whose subject matches the server's
 * issuer, one per handshake, so the full store costs no more RAM than the
 * minimal one.
 *
 * WHAT "MINIMAL" COVERS. Every root in it was either observed in a real
 * station's certificate chain or is one of the general-purpose roots that
 * public APIs chain to. The stations were probed rather than guessed; see the
 * list in tools/gencertstore.py, which also regenerates these tables from the
 * current Mozilla bundle.
 *
 * setCACert() STILL WINS. A sketch that names its own root gets exactly that
 * root and nothing else, whatever store is compiled in. Narrowing trust to
 * one CA you chose is stricter than trusting 121, and a sketch that has gone
 * to the trouble of saying so should not be silently widened.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

#ifndef CH32H4_CA_STORE
#define CH32H4_CA_STORE 0
#endif

#define CH32H4_CA_STORE_NONE    0
#define CH32H4_CA_STORE_MINIMAL 1
#define CH32H4_CA_STORE_FULL    2

/* One root: the certificate, and its subject name pre-extracted so a lookup
 * can find the right one with a memcmp instead of parsing all of them. */
typedef struct {
    const uint8_t *der;
    size_t der_len;
    const uint8_t *subject;
    size_t subject_len;
} ch32h4_ca_entry_t;

/* How many roots are compiled in. Zero when no store was selected, which is
 * what a sketch can test to print something useful rather than failing a
 * handshake for a reason the user cannot see. */
size_t ch32h4_ca_store_size(void);

#if defined(__cplusplus)
struct mbedtls_ssl_config;
/* Point an mbedtls config at the built-in store. False when no store is
 * compiled in, which leaves the caller's own arrangements untouched. */
bool ch32h4_ca_store_install(mbedtls_ssl_config *conf);
#endif
