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
