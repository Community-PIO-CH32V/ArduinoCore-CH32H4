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
    delay(2);
    Serial1.print("sweep_rclk="); Serial1.println(PSRAM.clock());
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

  } else if (!strncmp(cmd, "readperf ", 9)) {
    /* readperf <n> -- the two read paths over the same n bytes.
     *
     * memcpy from the window pays no mode flip but only manages ~2.9 MB/s,
     * because discrete CPU loads do not keep the controller streaming. DMA
     * runs at the line rate but pays a flip at each end. Where they cross is
     * what PSRAM_READ_DMA_THRESHOLD encodes, so it is measured, not guessed.
     */
    uint32_t n = (uint32_t)strtoul(cmd + 9, nullptr, 0);
    if (n > 65500) { n = 65500; }
    n &= ~3u;
    uint8_t *seed = bigA, *got = bigB;
    const uint32_t a = 0x500000u;
    for (uint32_t i = 0; i < n; i++) { seed[i] = pat(a + i * 5 + 3); }
    PSRAM.write(a, seed, n);

    /* Misaligned destination, so read() must use the window. */
    memset(got, 0, n + 1);
    uint32_t t0 = micros();
    PSRAM.read(a, got + 1, n);
    const uint32_t mc = micros() - t0;
    const int mc_ok = memcmp(seed, got + 1, n) == 0 ? 1 : 0;

    /* DMA forced, so the crossover can be found below the threshold too. */
    memset(got, 0, n + 1);
    t0 = micros();
    const size_t dn = PSRAM.readViaDMA(a, got, n);
    const uint32_t dm = micros() - t0;
    const int dm_ok = (dn == n && memcmp(seed, got, n) == 0) ? 1 : 0;

    Serial1.print("bytes="); Serial1.println(n);
    Serial1.print("memcpy_us="); Serial1.println(mc);
    Serial1.print("memcpy_ok="); Serial1.println(mc_ok);
    Serial1.print("dma_us="); Serial1.println(dm);
    Serial1.print("dma_ok="); Serial1.println(dm_ok);

  } else if (!strncmp(cmd, "writeperf ", 10)) {
    /* DMA against polling, over the same bytes with one mode flip each.
     *
     * The polled half is forced by handing write() a MISALIGNED source, not by
     * chopping the buffer into sub-threshold pieces. Chopping would charge the
     * polled side thousands of extra mode flips and let this pass even if DMA
     * were the slower path -- which is the whole thing the test exists to
     * detect.
     */
    uint32_t n = (uint32_t)strtoul(cmd + 10, nullptr, 0);
    if (n > 65500) { n = 65500; }
    n &= ~3u;                        /* the DMA path needs a whole word count */
    uint8_t *buf = bigA, *back = bigB;
    for (uint32_t i = 0; i < n + 1; i++) { buf[i] = pat(i * 3 + 1); }

    uint32_t t0 = micros();
    PSRAM.write(0x100000, buf, n);
    uint32_t dma = micros() - t0;
    memset(back, 0, n);
    PSRAM.read(0x100000, back, n);
    const int m1 = memcmp(buf, back, n) == 0 ? 1 : 0;

    t0 = micros();
    PSRAM.write(0x200000, buf + 1, n);
    uint32_t poll = micros() - t0;
    memset(back, 0, n);
    PSRAM.read(0x200000, back, n);
    const int m2 = memcmp(buf + 1, back, n) == 0 ? 1 : 0;

    Serial1.print("bytes="); Serial1.println(n);
    Serial1.print("dma_us="); Serial1.println(dma);
    Serial1.print("poll_us="); Serial1.println(poll);
    Serial1.print("match="); Serial1.println(m1);
    Serial1.print("poll_match="); Serial1.println(m2);

  } else if (!strncmp(cmd, "speed ", 6)) {
    /* speed <clockHz> -- time a 64 KB memory-mapped read at one clock.
     *
     * This measures the SCLK the controller is ACTUALLY producing, rather
     * than the one begin() computed. Memory-mapped reads run at the line
     * rate, so elapsed time is a direct read of the clock: 65536 bytes at
     * f x 4 bits should take 131072/f seconds. If a requested 100 MHz comes
     * back taking as long as 25 MHz did, the prescaler did not do what the
     * arithmetic says and every conclusion drawn from that setting is void.
     */
    uint32_t hz = (uint32_t)strtoul(cmd + 6, nullptr, 0);
    const uint32_t base = 0x400000u;
    PSRAM.end();
    if (!PSRAM.begin(PSRAMClass::DEFAULT_CLOCK)) {
      Serial1.println("speed_begun=0"); Serial1.print("> "); return;
    }
    for (uint32_t off = 0; off < 65536u; off += 256) {
      uint8_t b[256];
      for (int j = 0; j < 256; j++) { b[j] = pat(base + off + j); }
      PSRAM.write(base + off, b, 256);
    }
    PSRAM.end();
    if (!PSRAM.begin(hz)) {
      Serial1.println("speed_begun=0"); PSRAM.begin(); Serial1.print("> "); return;
    }
    delay(2);
    const uint8_t *win = PSRAM.data();
    if (!win) {
      Serial1.println("speed_mapped=0");
      PSRAM.end(); PSRAM.begin(); Serial1.print("> "); return;
    }
    const uint32_t *p = (const uint32_t *)(win + base);
    uint32_t bad = 0, sink = 0;
    uint32_t t0 = micros();
    for (uint32_t i = 0; i < 65536u / 4u; i++) { sink += p[i]; }
    uint32_t us = micros() - t0;
    for (uint32_t i = 0; i < 65536u / 4u; i++) {
      uint32_t a = base + i * 4;
      uint32_t want = (uint32_t)pat(a) | ((uint32_t)pat(a + 1) << 8)
                    | ((uint32_t)pat(a + 2) << 16) | ((uint32_t)pat(a + 3) << 24);
      if (p[i] != want) { bad++; }
    }
    Serial1.print("speed_clock="); Serial1.println(PSRAM.clock());
    Serial1.print("speed_us="); Serial1.println(us);
    Serial1.print("speed_bad="); Serial1.println(bad);
    Serial1.print("speed_sink="); Serial1.println(sink != 0 ? 1 : 0);
    PSRAM.end();
    PSRAM.begin();

  } else if (!strcmp(cmd, "regs")) {
    /* Dumped before any access to the window, so it survives a hang: an AHB
       read to a stalled QSPI never returns and never faults. */
    Serial1.print("cr=0x");  Serial1.println(QSPI2->CR, HEX);
    Serial1.print("dcr=0x"); Serial1.println(QSPI2->DCR, HEX);
    Serial1.print("sr=0x");  Serial1.println(QSPI2->SR, HEX);
    Serial1.print("ccr=0x"); Serial1.println(QSPI2->CCR, HEX);
    Serial1.print("dlr=0x"); Serial1.println(QSPI2->DLR, HEX);
    Serial1.print("mapped="); Serial1.println(PSRAM.mapped() ? 1 : 0);

  } else if (!strncmp(cmd, "mapcmp ", 7)) {
    /* The two read paths, side by side. read() is a memcpy from the window and
       the pointer is a raw CPU load, so this catches the window being mapped
       somewhere other than where data() claims. */
    uint32_t a = (uint32_t)strtoul(cmd + 7, nullptr, 0);
    uint8_t viaRead[64];
    PSRAM.read(a, viaRead, 64);
    const uint8_t *base = PSRAM.data();
    /* Report before dereferencing. If the window is not live the pointer is
       null, and touching it anyway would hang the CPU with nothing printed. */
    Serial1.print("base=0x"); Serial1.println((uint32_t)base, HEX);
    int bad = 64;
    if (base) {
      bad = 0;
      for (int i = 0; i < 64; i++) {
        if (base[a + i] != viaRead[i]) { bad++; }
      }
    }
    Serial1.print("mismatch="); Serial1.println(bad);

  } else if (!strncmp(cmd, "burst ", 6)) {
    /* burst <bytes> [timeout_counter]
       Seed witness rows across the array, then read one long uninterrupted run
       from a region that does not overlap them. The witnesses are the tCEM
       check: a refresh missed during the burst shows up as damage somewhere
       other than where we were reading. */
    char *rest = nullptr;
    uint32_t n = (uint32_t)strtoul(cmd + 6, &rest, 0);
    int tc = 0;                 /* off is what the library ships */
    if (rest && *rest) { tc = (int)strtol(rest, nullptr, 0); }
    const uint32_t base = 0x400000u;
    for (int i = 0; i < 48; i++) {
      uint32_t a = (uint32_t)i * (PSRAMClass::CAPACITY / 48u);
      if (a >= base && a < base + 0x20000u) { a += 0x40000u; }
      a &= ~7u;
      uint8_t b[8];
      for (int j = 0; j < 8; j++) { b[j] = pat(a + j); }
      PSRAM.write(a, b, 8);
    }
    for (uint32_t off = 0; off < n; off += 256) {
      uint8_t b[256];
      for (int j = 0; j < 256; j++) { b[j] = pat(base + off + j); }
      PSRAM.write(base + off, b, 256);
    }
    /* Toggled after the writes, so it applies to the burst being measured.
       write() re-enters mapped mode and would otherwise re-arm it. */
    QSPI_TimeoutCounterCmd(QSPI2, tc ? ENABLE : DISABLE);
    const uint8_t *win = PSRAM.data();
    if (!win) {
      Serial1.println("burst_us=0");
      Serial1.println("burst_errors=-1");
      Serial1.println("witness_bad=-1");
      Serial1.print("timeout_counter="); Serial1.println(tc);
      Serial1.print("> ");
      return;
    }
    const uint32_t *p = (const uint32_t *)(win + base);
    uint32_t bad = 0;
    uint32_t t0 = micros();
    for (uint32_t i = 0; i < n / 4; i++) {
      uint32_t a = base + i * 4;
      uint32_t want = (uint32_t)pat(a) | ((uint32_t)pat(a + 1) << 8)
                    | ((uint32_t)pat(a + 2) << 16) | ((uint32_t)pat(a + 3) << 24);
      if (p[i] != want) { bad++; }
    }
    uint32_t us = micros() - t0;
    uint32_t wbad = 0;
    for (int i = 0; i < 48; i++) {
      uint32_t a = (uint32_t)i * (PSRAMClass::CAPACITY / 48u);
      if (a >= base && a < base + 0x20000u) { a += 0x40000u; }
      a &= ~7u;
      const uint8_t *q = win + a;
      for (int j = 0; j < 8; j++) {
        if (q[j] != pat(a + j)) { wbad++; }
      }
    }
    QSPI_TimeoutCounterCmd(QSPI2, ENABLE);
    Serial1.print("burst_us="); Serial1.println(us);
    Serial1.print("burst_errors="); Serial1.println(bad);
    Serial1.print("witness_bad="); Serial1.println(wbad);
    Serial1.print("timeout_counter="); Serial1.println(tc);

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
