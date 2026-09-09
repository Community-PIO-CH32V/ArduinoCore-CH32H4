/* The PSRAM library, on Serial1.
 *
 * Needs the ESP-PSRAM64H breakout on PE10..PE15. Without it every command
 * still answers; "begun=0 detected=0" is what a missing chip looks like.
 */
#include <Arduino.h>
#include <PSRAM.h>

static char line[80];
static int len = 0;

static uint8_t pat(uint32_t a) { return (uint8_t)(a * 7u + (a >> 11)); }

/* Shared by readperf and writeperf. Two 64 KB buffers each would be 256 KB and
   DTCM is 255 KB, so the benchmarks take turns rather than each owning a pair.
   The extra 4 bytes give readperf/writeperf a misaligned source to work with
   without running off the end. */
static uint8_t bigA[65536 + 4] __attribute__((aligned(4)));
static uint8_t bigB[65536 + 4] __attribute__((aligned(4)));

/* Applied after every begin() inside sweep, so undocumented CR bits and pad
   drive strength can be varied without touching the library. */
static uint32_t g_extraCR = 0;
static int g_padSpeed = -1;          /* -1 = leave as the library set it */
static int g_fthres = -1;            /* -1 = leave begin()'s value (0) */

static void applyExtra(void) {
  if (g_extraCR) { QSPI2->CR |= g_extraCR; }
  if (g_fthres >= 0) { QSPI_SetFIFOThreshold(QSPI2, (uint32_t)g_fthres); }
  if (g_padSpeed >= 0) {
    static const uint16_t sp[4] = { GPIO_Speed_Low, GPIO_Speed_Medium,
                                    GPIO_Speed_High, GPIO_Speed_Very_High };
    for (int i = 0; i < 6; i++) {
      GPIO_InitTypeDef g = {};
      g.GPIO_Pin = (uint16_t)(1u << (10 + i));
      g.GPIO_Speed = (GPIOSpeed_TypeDef)sp[g_padSpeed & 3];
      g.GPIO_Mode = GPIO_Mode_AF_PP;
      GPIO_Init(GPIOE, &g);
    }
  }
}

static void handle(const char *cmd) {
  if (!strcmp(cmd, "begin")) {
    PSRAM.end();
    bool ok = PSRAM.begin();
    Serial1.print("begun="); Serial1.println(ok ? 1 : 0);
    Serial1.print("detected="); Serial1.println(PSRAM.detected() ? 1 : 0);
    Serial1.print("size="); Serial1.println((uint32_t)PSRAM.size());
    Serial1.print("clock="); Serial1.println(PSRAM.clock());
    Serial1.print("mfid="); Serial1.println(PSRAM.manufacturerID(), HEX);
    uint8_t e[6]; PSRAM.eid(e);
    Serial1.print("eid=");
    for (int i = 0; i < 6; i++) {
      if (e[i] < 16) { Serial1.print('0'); }
      Serial1.print(e[i], HEX);
    }
    Serial1.println();

  } else if (!strcmp(cmd, "badclock")) {
    PSRAM.end();
    /* 1 Hz needs a divider far past the 8-bit prescaler, so begin() must
       refuse rather than silently clocking something else. */
    Serial1.print("begun="); Serial1.println(PSRAM.begin(1) ? 1 : 0);
    PSRAM.begin();

  } else if (!strcmp(cmd, "again")) {
    Serial1.print("begun="); Serial1.println(PSRAM.begin() ? 1 : 0);

  } else if (!strncmp(cmd, "rw ", 3)) {
    /* rw <addr> -- write a pattern and read it back at one address. */
    uint32_t a = (uint32_t)strtoul(cmd + 3, nullptr, 0);
    uint8_t w[64], r[64];
    for (int i = 0; i < 64; i++) { w[i] = pat(a + i); }
    size_t nw = PSRAM.write(a, w, 64);
    memset(r, 0, 64);
    size_t nr = PSRAM.read(a, r, 64);
    Serial1.print("wrote="); Serial1.println((uint32_t)nw);
    Serial1.print("read="); Serial1.println((uint32_t)nr);
    Serial1.print("match="); Serial1.println(memcmp(w, r, 64) == 0 ? 1 : 0);
    int first = -1, cnt = 0;
    for (int i = 0; i < 64; i++) {
      if (w[i] != r[i]) { if (first < 0) { first = i; } cnt++; }
    }
    Serial1.print("firstbad="); Serial1.println(first);
    Serial1.print("nbad="); Serial1.println(cnt);
    if (first >= 0) {
      Serial1.print("wantat="); for (int i=first;i<first+8&&i<64;i++){if(w[i]<16)Serial1.print('0');Serial1.print(w[i],HEX);} Serial1.println();
      Serial1.print("gotat="); for (int i=first;i<first+8&&i<64;i++){if(r[i]<16)Serial1.print('0');Serial1.print(r[i],HEX);} Serial1.println();
    }

  } else if (!strcmp(cmd, "clamp")) {
    /* A read starting inside but running off the end must come back short,
       and one starting past the end must return nothing at all. */
    uint8_t b[64];
    Serial1.print("tail=");
    Serial1.println((uint32_t)PSRAM.read(PSRAMClass::CAPACITY - 16, b, 64));
    Serial1.print("past=");
    Serial1.println((uint32_t)PSRAM.read(PSRAMClass::CAPACITY, b, 8));

  } else if (!strcmp(cmd, "density")) {
    /* A unique byte at every power-of-two boundary, written first and
       verified afterwards. Writing all of them before reading any is the
       point: a stuck address line makes two boundaries collide, and that only
       shows if the later write can overwrite the earlier one. */
    for (int i = 0; i < 23; i++) {
      uint8_t v = (uint8_t)(0xA0 + i);
      PSRAM.write(1u << i, &v, 1);
    }
    uint8_t zero = 0x5C;
    PSRAM.write(0, &zero, 1);
    int bad = 0;
    for (int i = 0; i < 23; i++) {
      uint8_t v = 0;
      PSRAM.read(1u << i, &v, 1);
      if (v != (uint8_t)(0xA0 + i)) { bad++; }
    }
    uint8_t v0 = 0;
    PSRAM.read(0, &v0, 1);
    Serial1.print("boundaries_bad="); Serial1.println(bad);
    Serial1.print("zero_ok="); Serial1.println(v0 == 0x5C ? 1 : 0);

  } else if (!strncmp(cmd, "sweep ", 6)) {
    /* sweep <clockHz> [w] -- error rate against transfer length at one clock.
     *
     * Only one direction runs at the test clock; the other runs at the
     * default 25 MHz. A write has no round trip and a read does, so clocking
     * them separately is what says which side owns the ceiling.
     *
     * THREE re-inits per run, not one per repetition. An earlier version
     * re-initialised around every transfer, which hung the board -- see the
     * memory-mapped hazard in docs/hazards.md. Everything is written first at
     * one clock, then everything is read at the other.
     *
     * Short reads are counted separately from wrong bytes. read() returns 0
     * when the window is not live, and scoring that as corruption is how a
     * known-good control can be made to look like a failure.
     */
    char *rest = nullptr;
    uint32_t hz = (uint32_t)strtoul(cmd + 6, &rest, 0);
    while (rest && *rest == ' ') { rest++; }
    const bool readAtClock = !(rest && *rest == 'w');
    static const uint16_t lens[] = { 1, 2, 4, 8, 16, 32, 64, 128, 256, 1024 };
    const unsigned NL = sizeof(lens) / sizeof(lens[0]);
    const int REPS = 8;
    static uint8_t wbuf[1024], rbuf[1024];

    const uint32_t wclk = readAtClock ? PSRAMClass::DEFAULT_CLOCK : hz;
    const uint32_t rclk = readAtClock ? hz : PSRAMClass::DEFAULT_CLOCK;

    /* Pass 1: write every length at every repetition, at the write clock. */
    PSRAM.end();
    if (!PSRAM.begin(wclk)) {
      Serial1.println("sweep_begun=0");
      PSRAM.begin(); Serial1.print("> "); return;
    }
    applyExtra();
    delay(2);
    Serial1.print("sweep_wclk="); Serial1.println(PSRAM.clock());
    for (unsigned li = 0; li < NL; li++) {
      for (int rep = 0; rep < REPS; rep++) {
        const uint32_t a = 0x100000u + li * 0x8000u + rep * 0x800u;
        for (uint32_t i = 0; i < lens[li]; i++) {
          wbuf[i] = (uint8_t)(pat(a + i) + rep);
        }
        PSRAM.write(a, wbuf, lens[li]);
      }
    }

    /* Pass 2: read it all back at the read clock. */
    PSRAM.end();
    if (!PSRAM.begin(rclk)) {
      Serial1.println("sweep_begun=0");
      PSRAM.begin(); Serial1.print("> "); return;
    }
    applyExtra();
    delay(2);
    Serial1.print("sweep_rclk="); Serial1.println(PSRAM.clock());
    Serial1.print("sweep_cr=0x"); Serial1.println(QSPI2->CR, HEX);
    Serial1.print("sweep_pad="); Serial1.println(g_padSpeed);
    Serial1.print("sweep_dir="); Serial1.println(readAtClock ? "r" : "w");
    for (unsigned li = 0; li < NL; li++) {
      const uint32_t n = lens[li];
      int badReps = 0, shortReads = 0;
      uint32_t badBytes = 0;
      for (int rep = 0; rep < REPS; rep++) {
        const uint32_t a = 0x100000u + li * 0x8000u + rep * 0x800u;
        for (uint32_t i = 0; i < n; i++) {
          wbuf[i] = (uint8_t)(pat(a + i) + rep);
        }
        memset(rbuf, 0, n);
        if (PSRAM.read(a, rbuf, n) != n) { shortReads++; continue; }
        uint32_t bb = 0;
        for (uint32_t i = 0; i < n; i++) {
          if (rbuf[i] != wbuf[i]) { bb++; }
        }
        if (bb) { badReps++; badBytes += bb; }
      }
      /* Printed as each length finishes, so a hang says where it happened. */
      Serial1.print("l"); Serial1.print(n); Serial1.print("=");
      Serial1.print(badReps); Serial1.print(",");
      Serial1.print(badBytes); Serial1.print(",");
      Serial1.println(shortReads);
    }
    PSRAM.end();
    PSRAM.begin();
    Serial1.println("sweep_done=1");

  } else if (!strcmp(cmd, "unaligned")) {
    /* Read the same bytes starting at every offset within a word. The device
       addresses bytes, so all four must agree with what was written. */
    const uint32_t a = 0x610000u;
    uint8_t w[64], r[16];
    for (int i = 0; i < 64; i++) { w[i] = (uint8_t)(0x10 + i); }
    PSRAM.write(a, w, 64);
    for (uint32_t off = 0; off < 4; off++) {
      memset(r, 0, 16);
      PSRAM.read(a + off, r, 16);
      Serial1.print("off"); Serial1.print(off); Serial1.print("=");
      Serial1.print(memcmp(r, w + off, 16) == 0 ? 1 : 0);
      Serial1.print(",");
      for (int i = 0; i < 8; i++) {
        if (r[i] < 16) { Serial1.print('0'); }
        Serial1.print(r[i], HEX);
      }
      Serial1.println();
    }

  } else if (!strncmp(cmd, "ragged ", 7)) {
    /* ragged <n> -- a non-word-multiple round trip, plus a check that the
       write stopped exactly where it was told to.
       Neighbours are seeded with a POSITION-DEPENDENT value, not a constant:
       a uniform seed cannot distinguish "these bytes were overwritten" from
       "these bytes were read back shifted", and the first attempt at this
       test could not tell those apart. */
    uint32_t n = (uint32_t)strtoul(cmd + 7, nullptr, 0);
    if (n > 4096) { n = 4096; }
    const uint32_t a = 0x600000u;
    uint8_t *w = bigA, *r = bigB;
    for (uint32_t i = 0; i < n + 16; i++) { w[i] = (uint8_t)(0xC0 + (i & 15)); }
    PSRAM.write(a, w, n + 16);
    for (uint32_t i = 0; i < n; i++) { w[i] = pat(a + i * 11 + 5); }
    const size_t nw = PSRAM.write(a, w, n);
    memset(r, 0, n + 16);
    const size_t nr = PSRAM.read(a, r, n);

    /* Read the neighbours from a WORD-ALIGNED base covering them, so an
       unaligned read address cannot be blamed for what the write did. */
    const uint32_t nbase = (a + n) & ~3u;
    uint8_t near[32];
    PSRAM.read(nbase, near, 32);
    int nb = 1;
    for (uint32_t i = a + n; i < a + n + 8; i++) {
      if (near[i - nbase] != (uint8_t)(0xC0 + ((i - a) & 15))) { nb = 0; }
    }
    Serial1.print("wrote="); Serial1.println((uint32_t)nw);
    Serial1.print("read="); Serial1.println((uint32_t)nr);
    Serial1.print("match="); Serial1.println(memcmp(w, r, n) == 0 ? 1 : 0);
    Serial1.print("neighbour_ok="); Serial1.println(nb);
    Serial1.print("nbase_off="); Serial1.println((a + n) - nbase);
    Serial1.print("near=");
    for (int i = 0; i < 16; i++) {
      if (near[i] < 16) { Serial1.print('0'); }
      Serial1.print(near[i], HEX);
    }
    Serial1.println();

  } else if (!strncmp(cmd, "readperf ", 9)) {
    /* readperf <n> -- read throughput, aligned and unaligned.
     *
     * Both go through DMA now; the unaligned one additionally pays a copy
     * through the library's bounce buffer, which is the only cost difference
     * left in the API. */
    uint32_t n = (uint32_t)strtoul(cmd + 9, nullptr, 0);
    if (n > 65500) { n = 65500; }
    uint8_t *seed = bigA, *got = bigB;
    const uint32_t a = 0x500000u;
    for (uint32_t i = 0; i < n; i++) { seed[i] = pat(a + i * 5 + 3); }
    PSRAM.write(a, seed, n);

    memset(got, 0, n + 1);
    uint32_t t0 = micros();
    const size_t an = PSRAM.read(a, got, n);
    const uint32_t al = micros() - t0;
    const int al_ok = (an == n && memcmp(seed, got, n) == 0) ? 1 : 0;

    memset(got, 0, n + 1);
    t0 = micros();
    const size_t un = PSRAM.read(a, got + 1, n);
    const uint32_t ul = micros() - t0;
    const int ul_ok = (un == n && memcmp(seed, got + 1, n) == 0) ? 1 : 0;

    Serial1.print("bytes="); Serial1.println(n);
    Serial1.print("aligned_us="); Serial1.println(al);
    Serial1.print("aligned_ok="); Serial1.println(al_ok);
    Serial1.print("unaligned_us="); Serial1.println(ul);
    Serial1.print("unaligned_ok="); Serial1.println(ul_ok);

  } else if (!strncmp(cmd, "writeperf ", 10)) {
    /* writeperf <n> -- write throughput, aligned and unaligned. */
    uint32_t n = (uint32_t)strtoul(cmd + 10, nullptr, 0);
    if (n > 65500) { n = 65500; }
    uint8_t *buf = bigA, *back = bigB;
    for (uint32_t i = 0; i < n + 1; i++) { buf[i] = pat(i * 3 + 1); }

    uint32_t t0 = micros();
    PSRAM.write(0x100000, buf, n);
    const uint32_t al = micros() - t0;
    memset(back, 0, n);
    PSRAM.read(0x100000, back, n);
    const int al_ok = memcmp(buf, back, n) == 0 ? 1 : 0;

    t0 = micros();
    PSRAM.write(0x200000, buf + 1, n);
    const uint32_t ul = micros() - t0;
    memset(back, 0, n);
    PSRAM.read(0x200000, back, n);
    const int ul_ok = memcmp(buf + 1, back, n) == 0 ? 1 : 0;

    Serial1.print("bytes="); Serial1.println(n);
    Serial1.print("aligned_us="); Serial1.println(al);
    Serial1.print("aligned_ok="); Serial1.println(al_ok);
    Serial1.print("unaligned_us="); Serial1.println(ul);
    Serial1.print("unaligned_ok="); Serial1.println(ul_ok);

  } else if (!strncmp(cmd, "speed ", 6)) {
    /* speed <clockHz> -- time and verify a 64 KB read at one clock.
     *
     * Elapsed time is a direct read of the SCLK the controller is actually
     * producing, since DMA transfers run at the line rate: 65536 bytes at
     * f x 4 bits takes 131072/f seconds. If a requested clock comes back
     * taking as long as a slower one, the prescaler did not do what the
     * arithmetic says and any conclusion drawn from that setting is void. */
    uint32_t hz = (uint32_t)strtoul(cmd + 6, nullptr, 0);
    const uint32_t base = 0x400000u;
    uint8_t *seed = bigA, *got = bigB;
    for (uint32_t i = 0; i < 65536u; i++) { seed[i] = pat(base + i); }
    PSRAM.end();
    if (!PSRAM.begin(PSRAMClass::DEFAULT_CLOCK)) {
      Serial1.println("speed_begun=0"); Serial1.print("> "); return;
    }
    PSRAM.write(base, seed, 65536);
    PSRAM.end();
    if (!PSRAM.begin(hz)) {
      Serial1.println("speed_begun=0"); PSRAM.begin(); Serial1.print("> "); return;
    }
    applyExtra();
    delay(2);
    memset(got, 0, 65536);
    uint32_t t0 = micros();
    PSRAM.read(base, got, 65536);
    const uint32_t us = micros() - t0;
    uint32_t bad = 0;
    for (uint32_t i = 0; i < 65536u; i++) { if (got[i] != seed[i]) { bad++; } }
    Serial1.print("speed_clock="); Serial1.println(PSRAM.clock());
    Serial1.print("speed_us="); Serial1.println(us);
    Serial1.print("speed_bad="); Serial1.println(bad);
    PSRAM.end();
    PSRAM.begin();

  } else if (!strncmp(cmd, "extracr ", 8)) {
    g_extraCR = (uint32_t)strtoul(cmd + 8, nullptr, 0);
    Serial1.print("extracr=0x"); Serial1.println(g_extraCR, HEX);

  } else if (!strncmp(cmd, "fthres ", 7)) {
    g_fthres = (int)strtol(cmd + 7, nullptr, 0);
    Serial1.print("fthres="); Serial1.println(g_fthres);

  } else if (!strncmp(cmd, "padspeed ", 9)) {
    g_padSpeed = (int)strtol(cmd + 9, nullptr, 0);
    Serial1.print("padspeed="); Serial1.println(g_padSpeed);

  } else if (!strcmp(cmd, "probe")) {
    /* Which bits of CR and DCR are actually implemented.
     *
     * The register map is STM32 QUADSPI's exactly, and WCH demonstrably put
     * their own bits in ST's reserved positions -- QSPI_EnableQuad() sets
     * CR bit 13, which ST reserves. So writability says more than the header
     * does. Writing all-ones then all-zeros and reading back gives the set of
     * bits that hold a value.
     *
     * Only CR and DCR, and only with the peripheral disabled. CCR is left
     * alone deliberately: writing it configures a transaction, and an FMODE
     * of 11 would arm memory-mapped mode.
     */
    PSRAM.end();
    ch32h4_clock_enable(CH32_BUS_HB1, RCC_HB1Periph_QSPI2);
    QSPI_Cmd(QSPI2, DISABLE);
    struct { const char *name; volatile uint32_t *reg; } rs[] = {
      { "cr",   &QSPI2->CR   },
      { "dcr",  &QSPI2->DCR  },
      { "pir",  &QSPI2->PIR  },
      { "lptr", &QSPI2->LPTR },
    };
    for (unsigned i = 0; i < 4; i++) {
      const uint32_t saved = *rs[i].reg;
      *rs[i].reg = 0xFFFFFFFFu;
      const uint32_t ones = *rs[i].reg;
      *rs[i].reg = 0x00000000u;
      const uint32_t zeros = *rs[i].reg;
      *rs[i].reg = saved;
      Serial1.print(rs[i].name); Serial1.print("_rst=0x");
      Serial1.println(saved, HEX);
      Serial1.print(rs[i].name); Serial1.print("_ones=0x");
      Serial1.println(ones, HEX);
      Serial1.print(rs[i].name); Serial1.print("_zeros=0x");
      Serial1.println(zeros, HEX);
      /* Bits that took a 1 and also took a 0 are read/write. */
      Serial1.print(rs[i].name); Serial1.print("_rw=0x");
      Serial1.println(ones & ~zeros, HEX);
    }
    PSRAM.begin();

  } else if (!strcmp(cmd, "regs")) {
    /* Dumped before any access to the window, so it survives a hang: an AHB
       read to a stalled QSPI never returns and never faults. */
    Serial1.print("cr=0x");  Serial1.println(QSPI2->CR, HEX);
    Serial1.print("dcr=0x"); Serial1.println(QSPI2->DCR, HEX);
    Serial1.print("sr=0x");  Serial1.println(QSPI2->SR, HEX);
    Serial1.print("ccr=0x"); Serial1.println(QSPI2->CCR, HEX);
    Serial1.print("dlr=0x"); Serial1.println(QSPI2->DLR, HEX);

  } else if (!strncmp(cmd, "burst ", 6)) {
    /* burst <bytes> -- the tCEM regression, now over a DMA read.
       Seed witness rows across the array, then read one long uninterrupted
       run from a region that does not overlap them. A refresh missed during
       the burst shows up as damage somewhere other than where we were
       reading, which is why the witnesses exist. */
    uint32_t n = (uint32_t)strtoul(cmd + 6, nullptr, 0);
    if (n > 65536) { n = 65536; }
    const uint32_t base = 0x400000u;
    uint8_t *seed = bigA, *got = bigB;
    for (int i = 0; i < 48; i++) {
      uint32_t a = (uint32_t)i * (PSRAMClass::CAPACITY / 48u);
      if (a >= base && a < base + 0x20000u) { a += 0x40000u; }
      a &= ~7u;
      uint8_t b[8];
      for (int j = 0; j < 8; j++) { b[j] = pat(a + j); }
      PSRAM.write(a, b, 8);
    }
    for (uint32_t i = 0; i < n; i++) { seed[i] = pat(base + i); }
    PSRAM.write(base, seed, n);

    memset(got, 0, n);
    uint32_t t0 = micros();
    PSRAM.read(base, got, n);
    uint32_t us = micros() - t0;
    uint32_t bad = 0;
    for (uint32_t i = 0; i < n; i++) { if (got[i] != seed[i]) { bad++; } }

    uint32_t wbad = 0;
    for (int i = 0; i < 48; i++) {
      uint32_t a = (uint32_t)i * (PSRAMClass::CAPACITY / 48u);
      if (a >= base && a < base + 0x20000u) { a += 0x40000u; }
      a &= ~7u;
      uint8_t q[8];
      PSRAM.read(a, q, 8);
      for (int j = 0; j < 8; j++) {
        if (q[j] != pat(a + j)) { wbad++; }
      }
    }
    Serial1.print("burst_us="); Serial1.println(us);
    Serial1.print("burst_errors="); Serial1.println(bad);
    Serial1.print("witness_bad="); Serial1.println(wbad);

  } else {
    Serial1.print("unknown: "); Serial1.println(cmd);
  }
  Serial1.print("> ");
}

void setup() {
  Serial1.begin(115200);
  Serial1.println();
  Serial1.println("psram test");
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
