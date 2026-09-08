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
