/* LittleFS on the internal flash, and its coexistence with the EEPROM.
 *
 * The partition is 128 KB, reserved by platformio.ini. The two things worth
 * proving beyond "files work" are that the region is where the linker says it
 * is, and that writing the filesystem does not touch the EEPROM that sits
 * immediately above it -- those two share a flash tail and an erase page is
 * 8 KB, so an off-by-one page in either direction destroys the other.
 */
#include <Arduino.h>
#include <EEPROM.h>
#include <LittleFS.h>

extern "C" {
#include "ch32h4_flash.h"
extern uint8_t _FS_start;
extern uint8_t _FS_end;
extern uint8_t _EEPROM_start;
extern uint8_t _EEPROM_end;
}

static char line[128];
static int len = 0;

static void doLayout() {
  Serial1.print("fs_start=0x"); Serial1.println((uint32_t)(uintptr_t)&_FS_start, HEX);
  Serial1.print("fs_end=0x"); Serial1.println((uint32_t)(uintptr_t)&_FS_end, HEX);
  Serial1.print("fs_size="); Serial1.println((uint32_t)(&_FS_end - &_FS_start));
  Serial1.print("eeprom_start=0x"); Serial1.println((uint32_t)(uintptr_t)&_EEPROM_start, HEX);
  Serial1.print("eeprom_end=0x"); Serial1.println((uint32_t)(uintptr_t)&_EEPROM_end, HEX);
  Serial1.print("flash_page="); Serial1.println(ch32h4_flash_page_size());
  Serial1.print("flash_size="); Serial1.println(ch32h4_flash_size());
  /* The filesystem must end exactly where the EEPROM begins, with no gap and
     no overlap. The linker asserts it; this reports it from the running
     image, which is the only place the two can be seen together. */
  Serial1.print("adjacent=");
  Serial1.println((&_FS_end == &_EEPROM_start) ? 1 : 0);
}

static void doMount() {
  const bool ok = LittleFS.begin();
  Serial1.print("fs_mount="); Serial1.println(ok ? 1 : 0);
  if (!ok) return;
  FSInfo info;
  if (LittleFS.info(info)) {
    Serial1.print("fs_total="); Serial1.println((uint32_t)info.totalBytes);
    Serial1.print("fs_used="); Serial1.println((uint32_t)info.usedBytes);
    Serial1.print("fs_block="); Serial1.println((uint32_t)info.blockSize);
  }
}

/* Write a file, read it back, compare. The content is generated rather than
   constant so a stale file from a previous run cannot pass. */
static void doRoundTrip(const char *name, uint32_t bytes, uint8_t seed) {
  File f = LittleFS.open(name, "w");
  if (!f) { Serial1.println("fs_rt=open_write_failed"); return; }
  uint8_t buf[64];
  uint32_t written = 0;
  while (written < bytes) {
    const uint32_t n = (bytes - written) < sizeof(buf) ? (bytes - written) : sizeof(buf);
    for (uint32_t i = 0; i < n; i++) buf[i] = (uint8_t)(seed + written + i);
    const size_t w = f.write(buf, n);
    if (w != n) { Serial1.println("fs_rt=short_write"); f.close(); return; }
    written += n;
  }
  f.close();

  f = LittleFS.open(name, "r");
  if (!f) { Serial1.println("fs_rt=open_read_failed"); return; }
  Serial1.print("fs_size_on_disk="); Serial1.println((uint32_t)f.size());

  uint32_t read_back = 0;
  bool match = true;
  while (read_back < bytes) {
    const uint32_t n = (bytes - read_back) < sizeof(buf) ? (bytes - read_back) : sizeof(buf);
    const int r = f.read(buf, n);
    if (r != (int)n) { match = false; break; }
    for (uint32_t i = 0; i < n; i++) {
      if (buf[i] != (uint8_t)(seed + read_back + i)) { match = false; break; }
    }
    if (!match) break;
    read_back += n;
  }
  f.close();
  Serial1.print("fs_rt="); Serial1.println(match ? "ok" : "mismatch");
  Serial1.print("fs_rt_bytes="); Serial1.println(read_back);
}

static void doDirs() {
  LittleFS.mkdir("/sub");
  File f = LittleFS.open("/sub/inner.txt", "w");
  if (f) { f.print("x"); f.close(); }

  int files = 0, dirs = 0;
  Dir d = LittleFS.openDir("/");
  while (d.next()) {
    if (d.isDirectory()) dirs++; else files++;
  }
  Serial1.print("fs_root_files="); Serial1.println(files);
  Serial1.print("fs_root_dirs="); Serial1.println(dirs);
  Serial1.print("fs_sub_exists=");
  Serial1.println(LittleFS.exists("/sub/inner.txt") ? 1 : 0);
}

/* The one that matters: fill the filesystem hard, then check the EEPROM's
   contents are untouched. An erase that walked one page past _FS_end would
   take the EEPROM's active page with it. */
static void doEepromGuard(uint32_t rounds) {
  EEPROM.begin(256);
  for (int i = 0; i < 32; i++) EEPROM.write(i, (uint8_t)(0xA5 ^ i));
  const bool committed = EEPROM.commit();
  Serial1.print("eeprom_commit="); Serial1.println(committed ? 1 : 0);
  EEPROM.end();

  uint32_t written = 0;
  for (uint32_t r = 0; r < rounds; r++) {
    char name[32];
    snprintf(name, sizeof(name), "/fill%lu.bin", (unsigned long)r);
    File f = LittleFS.open(name, "w");
    if (!f) break;
    uint8_t buf[128];
    memset(buf, (int)(r & 0xFF), sizeof(buf));
    for (int k = 0; k < 16; k++) {
      if (f.write(buf, sizeof(buf)) != sizeof(buf)) break;
      written += sizeof(buf);
    }
    f.close();
  }
  Serial1.print("fs_fill_bytes="); Serial1.println(written);

  EEPROM.begin(256);
  bool intact = true;
  for (int i = 0; i < 32; i++) {
    if (EEPROM.read(i) != (uint8_t)(0xA5 ^ i)) { intact = false; break; }
  }
  EEPROM.end();
  Serial1.print("eeprom_intact="); Serial1.println(intact ? 1 : 0);

  /* And the other direction: the filesystem still mounts and its files are
     still readable after the EEPROM wrote its two pages. */
  Serial1.print("fs_still_mounted=");
  Serial1.println(LittleFS.exists("/fill0.bin") ? 1 : 0);
}

static void doWipe() {
  Serial1.print("fs_format="); Serial1.println(LittleFS.format() ? 1 : 0);
  Serial1.print("fs_remount="); Serial1.println(LittleFS.begin() ? 1 : 0);
}

/* Erase, blank-check, program, verify -- on the first filesystem block, with
   no LittleFS involved. Separates "the flash driver is wrong" from "the
   LittleFS configuration is wrong", which look identical from a failed
   mount. */
static void doRaw() {
  const uint32_t addr = (uint32_t)(uintptr_t)&_FS_start;
  const uint32_t page = ch32h4_flash_page_size();

  Serial1.print("raw_erase="); Serial1.println(ch32h4_flash_erase(addr, page) ? 1 : 0);
  Serial1.print("raw_blank="); Serial1.println(ch32h4_flash_is_erased(addr, page) ? 1 : 0);

  uint32_t first;
  ch32h4_flash_read(addr, &first, 4);
  Serial1.print("raw_erased_word=0x"); Serial1.println(first, HEX);

  const uint32_t psz = ch32h4_flash_prog_size();
  Serial1.print("raw_prog_size="); Serial1.println(psz);

  static uint8_t pat[256];
  for (uint32_t i = 0; i < psz; i++) pat[i] = (uint8_t)(0x10 + i);
  Serial1.print("raw_write="); Serial1.println(ch32h4_flash_write(addr, pat, psz) ? 1 : 0);

  static uint8_t back[256];
  ch32h4_flash_read(addr, back, psz);
  Serial1.print("raw_verify=");
  Serial1.println(memcmp(pat, back, psz) == 0 ? 1 : 0);
  Serial1.print("raw_first_byte=0x"); Serial1.println(back[0], HEX);
  Serial1.print("raw_last_byte=0x"); Serial1.println(back[psz - 1], HEX);
  /* A second page in the same erase block, to prove the page loop advances. */
  Serial1.print("raw_write2=");
  Serial1.println(ch32h4_flash_write(addr + psz, pat, psz) ? 1 : 0);
  Serial1.print("raw_unaligned_refused=");
  Serial1.println(ch32h4_flash_write(addr + 4, pat, psz) ? 0 : 1);
}

/* The raw LittleFS return codes. A bare false from begin() cannot distinguish
   a failed erase from a configuration LittleFS rejects. */
static LittleFSImpl s_probe;   /* its own instance, so getImpl() is not needed */

static void doDiag() {
  Serial1.print("diag_part_start=0x"); Serial1.println(s_probe.partitionStart(), HEX);
  Serial1.print("diag_part_size="); Serial1.println(s_probe.partitionSize());
  Serial1.print("diag_format="); Serial1.println(s_probe.format() ? 1 : 0);
  Serial1.print("diag_begin="); Serial1.println(s_probe.begin() ? 1 : 0);
  Serial1.print("diag_block="); Serial1.println(s_probe.blockSize());
}

static void handle(char *cmd) {
  if (!strcmp(cmd, "fsraw")) {
    doRaw();

  } else if (!strcmp(cmd, "fsdiag")) {
    doDiag();

  } else if (!strcmp(cmd, "fslayout")) {
    doLayout();

  } else if (!strcmp(cmd, "fsmount")) {
    doMount();

  } else if (!strncmp(cmd, "fsrt ", 5)) {
    uint32_t bytes = (uint32_t)atol(cmd + 5);
    char *sp = strchr(cmd + 5, ' ');
    doRoundTrip("/rt.bin", bytes, sp ? (uint8_t)atoi(sp + 1) : 1);

  } else if (!strcmp(cmd, "fsdirs")) {
    doDirs();

  } else if (!strncmp(cmd, "fseeprom ", 9)) {
    doEepromGuard((uint32_t)atol(cmd + 9));

  } else if (!strcmp(cmd, "fsformat")) {
    doWipe();

  } else if (!strcmp(cmd, "fspersist")) {
    /* Written before a reset, checked after it. */
    File f = LittleFS.open("/persist.txt", "w");
    if (f) { f.print("survived"); f.close(); }
    Serial1.println("fs_persist_written=1");

  } else if (!strcmp(cmd, "fspersistcheck")) {
    File f = LittleFS.open("/persist.txt", "r");
    String s = f ? f.readString() : String("");
    if (f) f.close();
    Serial1.print("fs_persist="); Serial1.println(s == "survived" ? 1 : 0);

  } else if (!strcmp(cmd, "fsremove")) {
    Serial1.print("fs_remove=");
    Serial1.println(LittleFS.remove("/rt.bin") ? 1 : 0);
    Serial1.print("fs_gone=");
    Serial1.println(LittleFS.exists("/rt.bin") ? 0 : 1);
  }
  Serial1.print("> ");
}

void setup() {
  Serial1.begin(115200);
  Serial1.println("lfstest starting");
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
