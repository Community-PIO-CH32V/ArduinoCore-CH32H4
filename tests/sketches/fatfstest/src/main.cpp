/* The flash translation layer, and (from Task 3) the FAT filesystem on it.
 *
 * On Serial1 and without waiting for a host, like every other hardware test
 * sketch here: the thing under test must not need the thing testing it.
 *
 * The FTL commands come first and are tested on their own, because a failed
 * FAT mount looks identical whether the mapping layer lost a block or FatFs
 * rejected the geometry, and separating them is the only way to tell without
 * a debugger.
 */
#include <Arduino.h>
#include <string.h>

#include <ch32h4_ftl_flash.h>
#include <SPIFTL.h>

extern "C" {
#include "ch32h4_flash.h"
}

extern "C" char _FS_start[];
extern "C" char _FS_end[];

static CH32H4FTLFlash *flash = nullptr;
static SPIFTL *ftl = nullptr;

static void ftlCreate() {
  if (!ftl) {
    flash = new CH32H4FTLFlash((uint32_t)(uintptr_t)_FS_start,
                               (uint32_t)(_FS_end - _FS_start));
    ftl = new SPIFTL(flash, (int)flash->ebBytes());
  }
}

static void info() {
  Serial1.print("fs_start=0x");
  Serial1.println((uint32_t)(uintptr_t)_FS_start, HEX);
  Serial1.print("fs_size="); Serial1.println((uint32_t)(_FS_end - _FS_start));
  Serial1.print("eb_bytes="); Serial1.println(ch32h4_flash_page_size());
  Serial1.print("prog_size="); Serial1.println(ch32h4_flash_prog_size());
  if (ftl) {
    Serial1.print("ftl_lbas="); Serial1.println(ftl->lbaCount());
    Serial1.print("ftl_ebs="); Serial1.println(ftl->ebCount());
  }
}

/* A pattern that depends on the LBA, so a read returning the wrong block is a
   failure rather than a coincidence. The salt distinguishes a rewrite from
   the original contents of the same LBA. */
static void fillPattern(uint8_t *buf, int lba, uint8_t salt) {
  for (int i = 0; i < 512; i++) {
    buf[i] = (uint8_t)(lba * 7 + i * 3 + salt);
  }
}

static void ftlWrite(int lba, uint8_t salt) {
  uint8_t buf[512];
  fillPattern(buf, lba, salt);
  Serial1.print("ftl_write="); Serial1.println(ftl->write(lba, buf) ? 1 : 0);
}

static void ftlVerify(int lba, uint8_t salt) {
  uint8_t buf[512];
  uint8_t want[512];
  fillPattern(want, lba, salt);
  bool ok = ftl->read(lba, buf);
  Serial1.print("ftl_read="); Serial1.println(ok ? 1 : 0);
  Serial1.print("ftl_match=");
  Serial1.println(ok && !memcmp(buf, want, 512) ? 1 : 0);
}

void setup() {
  Serial1.begin(115200);
  Serial1.println();
  Serial1.println("fatfstest starting");
  Serial1.print("> ");
}

void loop() {
  static String line;
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        int sp = line.indexOf(' ');
        String cmd = sp < 0 ? line : line.substring(0, sp);
        String arg = sp < 0 ? String() : line.substring(sp + 1);

        if (cmd == "info") {
          info();
        } else if (cmd == "ftlcreate") {
          ftlCreate();
          Serial1.println("ftl_create=1");
        } else if (cmd == "ftlformat") {
          ftlCreate();
          Serial1.print("ftl_format="); Serial1.println(ftl->format() ? 1 : 0);
        } else if (cmd == "ftlstart") {
          ftlCreate();
          Serial1.print("ftl_start="); Serial1.println(ftl->start() ? 1 : 0);
        } else if (cmd == "ftlpersist") {
          Serial1.print("ftl_persist=");
          Serial1.println(ftl && ftl->persist() ? 1 : 0);
        } else if (cmd == "ftliszero") {
          /* Actually all zeroes, not merely "not the pattern we wrote". An
             LBA with no mapping must read as zeroes -- that is what a
             filesystem expects from a block it has never written, and
             returning stale flash there would leak a previous filesystem's
             contents into a freshly formatted one. */
          uint8_t buf[512];
          bool ok = ftl->read(arg.toInt(), buf);
          bool zero = ok;
          for (int i = 0; zero && i < 512; i++) {
            zero = buf[i] == 0;
          }
          Serial1.print("ftl_read="); Serial1.println(ok ? 1 : 0);
          Serial1.print("ftl_zero="); Serial1.println(zero ? 1 : 0);
        } else if (cmd == "ftlstats") {
          /* Erase counts, which are the only evidence that garbage collection
             actually ran. A churn test that never forces a reclaim passes
             without exercising the operation most likely to lose data, and
             looks exactly like one that did. */
          int maxPE = 0, total = 0;
          for (int i = 0; ftl && i < ftl->ebCount(); i++) {
            int pe = ftl->getPECount(i);
            if (pe > maxPE) { maxPE = pe; }
            total += pe;
          }
          Serial1.print("ftl_max_pe="); Serial1.println(maxPE);
          Serial1.print("ftl_total_pe="); Serial1.println(total);
        } else if (cmd == "ftlcheck") {
          /* SPIFTL's own consistency check: crosslinked LBAs, empty-block
             accounting, wear spread. It prints its own complaints. */
          Serial1.print("ftl_check=");
          Serial1.println(ftl && ftl->check() ? 1 : 0);
        } else if (cmd == "ftlwrite") {
          ftlWrite(arg.toInt(), 0);
        } else if (cmd == "ftlverify") {
          ftlVerify(arg.toInt(), 0);
        } else if (cmd == "ftlrewrite") {
          ftlWrite(arg.toInt(), 0x5A);
        } else if (cmd == "ftlreverify") {
          ftlVerify(arg.toInt(), 0x5A);
        } else if (cmd == "ftlchurn") {
          /* Force garbage collection: write one LBA far more times than there
             are erase blocks, so the FTL must reclaim space to keep going.
             That is the operation which moves live data between blocks, and
             so the one most likely to lose it. */
          int n = arg.toInt();
          bool ok = true;
          uint8_t buf[512];
          for (int i = 0; i < n && ok; i++) {
            fillPattern(buf, 3, (uint8_t)i);
            ok = ftl->write(3, buf);
          }
          Serial1.print("ftl_churn="); Serial1.println(ok ? 1 : 0);
        } else {
          Serial1.print("unknown: "); Serial1.println(cmd);
        }
        line = "";
      }
      Serial1.print("> ");
    } else {
      line += c;
    }
  }
}
