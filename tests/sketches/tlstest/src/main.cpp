/* mbedTLS on this part: the ECDC AES accelerator, the TRNG as entropy, and a
 * TLS client that checks certificates against the RTC.
 *
 * The AES known-answer tests come first and matter most. The ECDC block's
 * ECB and CTR mode selectors are swapped relative to the vendor header
 * (openwch/ch32h417 issue #10), and the failure is silent: picking CTR when
 * you meant ECB gives a stream cipher under a constant keystream, which
 * encrypts, decrypts, round-trips, and is worthless. An all-zero plaintext is
 * the one input for which the two agree, which is exactly why the vectors
 * below use a non-zero one.
 */
#include <Arduino.h>
#include <EthernetClientSecure.h>
#include <LwipEthernet.h>

extern "C" {
#include "ch32h4_rng.h"
#include "ch32h4_rtc.h"
#include "mbedtls/aes.h"
#include "mbedtls/ctr_drbg.h"
#include "mbedtls/entropy.h"
#include "mbedtls/version.h"
}

static char line[512];
static int len = 0;

/* The CA the host-side test generates. Filled in over the console by
 * `cabegin` / `caline` / `caend`, because a PEM does not fit in one command
 * line and baking one into the firmware would make the test depend on a
 * certificate that expires. */
static char ca_pem[2048];
static size_t ca_len = 0;

static void hexDump(const char *key, const uint8_t *p, size_t n) {
  Serial1.print(key);
  Serial1.print("=");
  for (size_t i = 0; i < n; i++) {
    Serial1.print("0123456789abcdef"[p[i] >> 4]);
    Serial1.print("0123456789abcdef"[p[i] & 0xF]);
  }
  Serial1.println();
}

static int hexParse(const char *s, uint8_t *out, size_t max) {
  size_t n = 0;
  while (s[0] && s[1] && n < max) {
    auto nib = [](char c) -> int {
      if (c >= '0' && c <= '9') return c - '0';
      if (c >= 'a' && c <= 'f') return c - 'a' + 10;
      if (c >= 'A' && c <= 'F') return c - 'A' + 10;
      return -1;
    };
    int hi = nib(s[0]), lo = nib(s[1]);
    if (hi < 0 || lo < 0) break;
    out[n++] = (uint8_t)((hi << 4) | lo);
    s += 2;
  }
  return (int)n;
}

/* FIPS-197 appendix C. One vector per key length, and the plaintext is
   deliberately not all zeroes. */
static void aesKnownAnswers() {
  struct Vec { unsigned bits; const char *key, *pt, *ct; };
  static const Vec vecs[] = {
    { 128, "000102030405060708090a0b0c0d0e0f",
           "00112233445566778899aabbccddeeff",
           "69c4e0d86a7b0430d8cdb78070b4c55a" },
    { 192, "000102030405060708090a0b0c0d0e0f1011121314151617",
           "00112233445566778899aabbccddeeff",
           "dda97ca4864cdfe06eaf70a0ec0d7191" },
    { 256, "000102030405060708090a0b0c0d0e0f101112131415161718191a1b1c1d1e1f",
           "00112233445566778899aabbccddeeff",
           "8ea2b7ca516745bfeafc49904b496089" },
  };

  int pass = 0;
  for (const Vec &v : vecs) {
    uint8_t key[32], pt[16], want[16], got[16], back[16];
    hexParse(v.key, key, sizeof(key));
    hexParse(v.pt, pt, sizeof(pt));
    hexParse(v.ct, want, sizeof(want));

    mbedtls_aes_context ctx;
    mbedtls_aes_init(&ctx);
    mbedtls_aes_setkey_enc(&ctx, key, v.bits);
    mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, pt, got);
    bool enc_ok = memcmp(got, want, 16) == 0;

    mbedtls_aes_setkey_dec(&ctx, key, v.bits);
    mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_DECRYPT, want, back);
    bool dec_ok = memcmp(back, pt, 16) == 0;
    mbedtls_aes_free(&ctx);

    Serial1.print("aes"); Serial1.print(v.bits);
    Serial1.print("_enc="); Serial1.println(enc_ok ? "ok" : "FAIL");
    if (!enc_ok) hexDump("  got", got, 16);
    Serial1.print("aes"); Serial1.print(v.bits);
    Serial1.print("_dec="); Serial1.println(dec_ok ? "ok" : "FAIL");
    if (enc_ok && dec_ok) pass++;
  }
  Serial1.print("aes_vectors_passed="); Serial1.println(pass);
}

/* The all-zero plaintext, encrypted. This is the value that proves the mode
   selector: in ECB it is E(key, 0), in CTR-with-a-zero-counter it is also
   E(key, 0), so the two AGREE here -- which is why the vectors above use a
   non-zero plaintext and this is reported for information rather than
   asserted on. */
static void aesZeroBlock() {
  uint8_t key[16], zero[16] = {0}, out[16];
  hexParse("000102030405060708090a0b0c0d0e0f", key, sizeof(key));
  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);
  mbedtls_aes_setkey_enc(&ctx, key, 128);
  mbedtls_aes_crypt_ecb(&ctx, MBEDTLS_AES_ENCRYPT, zero, out);
  mbedtls_aes_free(&ctx);
  hexDump("aes_zero_block", out, 16);
}

static void aesCbcCtr() {
  uint8_t key[16], iv[16], pt[48], ct[48], back[48];
  hexParse("2b7e151628aed2a6abf7158809cf4f3c", key, sizeof(key));
  for (int i = 0; i < 16; i++) iv[i] = (uint8_t)i;
  for (int i = 0; i < 48; i++) pt[i] = (uint8_t)(i * 7 + 3);

  mbedtls_aes_context ctx;
  mbedtls_aes_init(&ctx);

  uint8_t ivw[16];
  memcpy(ivw, iv, 16);
  mbedtls_aes_setkey_enc(&ctx, key, 128);
  int e1 = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_ENCRYPT, 48, ivw, pt, ct);
  memcpy(ivw, iv, 16);
  mbedtls_aes_setkey_dec(&ctx, key, 128);
  int e2 = mbedtls_aes_crypt_cbc(&ctx, MBEDTLS_AES_DECRYPT, 48, ivw, ct, back);
  bool cbc_ok = (e1 == 0 && e2 == 0 && memcmp(back, pt, 48) == 0
                 && memcmp(ct, pt, 48) != 0);
  Serial1.print("aes_cbc="); Serial1.println(cbc_ok ? "ok" : "FAIL");

  uint8_t nonce[16], stream[16];
  size_t off = 0;
  memcpy(nonce, iv, 16);
  mbedtls_aes_setkey_enc(&ctx, key, 128);
  mbedtls_aes_crypt_ctr(&ctx, 48, &off, nonce, stream, pt, ct);
  off = 0;
  memcpy(nonce, iv, 16);
  mbedtls_aes_crypt_ctr(&ctx, 48, &off, nonce, stream, ct, back);
  bool ctr_ok = (memcmp(back, pt, 48) == 0 && memcmp(ct, pt, 48) != 0);
  Serial1.print("aes_ctr="); Serial1.println(ctr_ok ? "ok" : "FAIL");

  mbedtls_aes_free(&ctx);
}

static void drbgTest() {
  mbedtls_entropy_context ent;
  mbedtls_ctr_drbg_context drbg;
  mbedtls_entropy_init(&ent);
  mbedtls_ctr_drbg_init(&drbg);
  static const char pers[] = "ch32h4-test";
  int rc = mbedtls_ctr_drbg_seed(&drbg, mbedtls_entropy_func, &ent,
                                 (const unsigned char *)pers, sizeof(pers) - 1);
  Serial1.print("drbg_seed_rc="); Serial1.println(rc);

  uint8_t a[32], b[32];
  int r1 = mbedtls_ctr_drbg_random(&drbg, a, sizeof(a));
  int r2 = mbedtls_ctr_drbg_random(&drbg, b, sizeof(b));
  Serial1.print("drbg_rc="); Serial1.println(r1 == 0 && r2 == 0 ? 0 : -1);
  Serial1.print("drbg_differ=");
  Serial1.println(memcmp(a, b, sizeof(a)) != 0 ? 1 : 0);
  hexDump("drbg_a", a, 16);
  mbedtls_ctr_drbg_free(&drbg);
  mbedtls_entropy_free(&ent);
}

static void tlsConnect(char *args, bool insecure) {
  /* args: host port [expect_fail] */
  char *sp = strchr(args, ' ');
  if (!sp) { Serial1.println("tls=usage"); return; }
  *sp = '\0';
  uint16_t port = (uint16_t)atoi(sp + 1);

  EthernetClientSecure c;
  c.setHandshakeTimeout(25000);
  if (insecure) {
    c.setInsecure();
  } else if (ca_len) {
    ca_pem[ca_len] = '\0';
    c.setCACert(ca_pem);
  }

  uint32_t t0 = millis();
  int ok = c.connect(args, port);
  uint32_t ms = millis() - t0;

  Serial1.print("tls_connect="); Serial1.println(ok);
  Serial1.print("tls_ms="); Serial1.println(ms);
  Serial1.print("tls_verify=0x"); Serial1.println(c.verifyError(), HEX);
  Serial1.print("tls_verify_str="); Serial1.println(c.verifyErrorString());
  Serial1.print("tls_err="); Serial1.println(c.lastError());
  Serial1.print("tls_err_str="); Serial1.println(c.lastErrorString());
  if (!ok) return;

  String req = "GET / HTTP/1.0\r\nHost: ";
  req += args;
  req += "\r\nConnection: close\r\n\r\n";
  size_t wrote = c.write((const uint8_t *)req.c_str(), req.length());
  Serial1.print("tls_wrote="); Serial1.print(wrote);
  Serial1.print("/"); Serial1.println(req.length());
  Serial1.print("tls_err_after_write="); Serial1.println(c.lastError());
  Serial1.print("tls_conn_after_write="); Serial1.println(c.connected());

  /* The idiomatic Arduino shape, exactly as a sketch would write it. Both
     counters are reported so a mismatch between available() and read() shows
     up rather than being papered over by reading regardless. */
  String head;
  int total = 0, availSeen = 0;
  uint32_t start = millis();
  while (c.connected() && millis() - start < 8000 && total < 48) {
    int a = c.available();
    if (a > availSeen) availSeen = a;
    while (a-- > 0 && total < 48) {
      int ch = c.read();
      if (ch < 0) break;
      total++;
      head += (ch == 13 || ch == 10) ? ' ' : (char)ch;
    }
    yield();
  }
  Serial1.print("tls_bytes="); Serial1.println(total);
  Serial1.print("tls_avail_seen="); Serial1.println(availSeen);
  Serial1.print("tls_response="); Serial1.println(head);
  c.stop();
}

static void handle(char *cmd) {
  if (!strcmp(cmd, "tlsinfo")) {
    char v[16];
    mbedtls_version_get_string(v);
    Serial1.print("mbedtls_version="); Serial1.println(v);
    Serial1.print("rtc_is_set="); Serial1.println(ch32h4_rtc_is_set() ? 1 : 0);
    Serial1.print("heap_free="); Serial1.println((uint32_t)ch32h4_heap_free());

  } else if (!strcmp(cmd, "aestest")) {
    aesKnownAnswers();
    aesZeroBlock();
    aesCbcCtr();

  } else if (!strcmp(cmd, "drbgtest")) {
    drbgTest();

  } else if (!strcmp(cmd, "cabegin")) {
    ca_len = 0;
    Serial1.println("ca_reset=1");

  } else if (!strncmp(cmd, "caline ", 7)) {
    const char *p = cmd + 7;
    size_t n = strlen(p);
    if (ca_len + n + 2 < sizeof(ca_pem)) {
      memcpy(ca_pem + ca_len, p, n);
      ca_len += n;
      ca_pem[ca_len++] = '\n';
    }

  } else if (!strcmp(cmd, "caend")) {
    ca_pem[ca_len] = '\0';
    Serial1.print("ca_bytes="); Serial1.println((uint32_t)ca_len);

  } else if (!strncmp(cmd, "tlsverify ", 10)) {
    tlsConnect(cmd + 10, false);

  } else if (!strncmp(cmd, "tlsinsecure ", 12)) {
    tlsConnect(cmd + 12, true);

  } else if (!strncmp(cmd, "ntpsync ", 8)) {
    ch32h4_rtc_begin(CH32H4_RTC_SRC_LSE);
    NTP.begin(cmd + 8);
    Serial1.print("ntp_synced="); Serial1.println(NTP.waitSynced(20000) ? 1 : 0);
    Serial1.print("rtc_is_set="); Serial1.println(ch32h4_rtc_is_set() ? 1 : 0);

  } else if (!strncmp(cmd, "rtcset ", 7)) {
    ch32h4_rtc_begin(CH32H4_RTC_SRC_LSE);
    struct timeval tv = { (time_t)strtoul(cmd + 7, nullptr, 10), 0 };
    Serial1.print("rtc_set=");
    Serial1.println(settimeofday(&tv, nullptr) == 0 ? 1 : 0);

  } else if (!strcmp(cmd, "netinfo")) {
    Serial1.print("ip="); Serial1.println(Ethernet.localIP());
    Serial1.print("link="); Serial1.println((int)Ethernet.linkStatus());
  }
  Serial1.print("> ");
}

void setup() {
  Serial1.begin(115200);
  Serial1.println("tlstest starting");
  Ethernet.setHostname("ch32h417-tls");
  Serial1.print("dhcp="); Serial1.println(Ethernet.begin(15000));
  Serial1.print("ip="); Serial1.println(Ethernet.localIP());
  Serial1.println("tlstest ready");
  Serial1.print("> ");
}

void loop() {
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (len) { line[len] = '\0'; handle(line); len = 0; }
    } else if (len < (int)sizeof(line) - 1) {
      line[len++] = c;
    }
  }
  yield();
}
