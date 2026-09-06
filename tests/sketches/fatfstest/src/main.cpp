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
#include <FatFS.h>

extern "C" {
#include "ch32h4_flash.h"
}

extern "C" char _FS_start[];
extern "C" char _FS_end[];

/* THE SAME instance FatFS uses, not a second one.
 *
 * Two SPIFTL objects over one partition each keep their own map in RAM and
 * each believe it authoritative, so a write through one is invisible to the
 * other and the other will allocate over it. This sketch drives both the raw
 * layer and the filesystem above it, which is exactly the situation where
 * that would happen -- so there is one instance and both go through it. */
static SPIFTL *ftl = nullptr;

static void ftlCreate() {
  ftl = ch32h4_fatfs_ftl();
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
          ftlCreate();
          Serial1.print("ftl_persist=");
          Serial1.println(ftl && ftl->persist() ? 1 : 0);
        } else if (cmd == "fsbegin" || cmd == "fsnoautoformat") {
          if (cmd == "fsnoautoformat") {
            FatFS.setConfig(FatFSConfig().setAutoFormat(false));
          } else {
            FatFS.setConfig(FatFSConfig());
          }
          uint32_t t0 = millis();
          bool ok = FatFS.begin();
          Serial1.print("fs_mount="); Serial1.println(ok ? 1 : 0);
          Serial1.print("fs_mount_ms="); Serial1.println(millis() - t0);
          /* The string, not the enum: a test asserting fs_err==2 breaks
             silently the day someone inserts a value, and the string is what
             a person reading the log needs anyway. */
          Serial1.print("fs_err=");
          Serial1.println(ch32h4_fatfs_last_error_string());
        } else if (cmd == "fsend") {
          FatFS.end();
          Serial1.println("fs_end=1");
        } else if (cmd == "fsformat") {
          uint32_t t0 = millis();
          bool ok = FatFS.format();
          Serial1.print("fs_format="); Serial1.println(ok ? 1 : 0);
          Serial1.print("fs_format_ms="); Serial1.println(millis() - t0);
          Serial1.print("fs_err=");
          Serial1.println(ch32h4_fatfs_last_error_string());
        } else if (cmd == "fsinfo") {
          FSInfo fi;
          if (!FatFS.info(fi)) {
            Serial1.println("fs_info=0");
          } else {
            Serial1.println("fs_info=1");
            Serial1.print("fs_total_kb=");
            Serial1.println((uint32_t)(fi.totalBytes / 1024));
            Serial1.print("fs_used_kb=");
            Serial1.println((uint32_t)(fi.usedBytes / 1024));
            Serial1.print("fs_cluster="); Serial1.println((uint32_t)fi.blockSize);
          }
          Serial1.print("fs_lbas="); Serial1.println(ch32h4_fatfs_lba_count());
          Serial1.print("fs_syncs="); Serial1.println(ch32h4_fatfs_sync_count());
          Serial1.print("fs_sync_fails=");
          Serial1.println(ch32h4_fatfs_sync_failures());
        } else if (cmd == "fswrite") {
          /* Content derived from the name, so reading back the wrong file is
             a failure rather than a coincidence. */
          String path = "/" + arg + ".txt";
          File f = FatFS.open(path.c_str(), "w");
          if (!f) { Serial1.println("fs_rt=open_write_failed"); }
          else {
            size_t n = 0;
            for (int i = 0; i < 64; i++) {
              String line = arg + " line " + String(i) + "\n";
              n += f.print(line);
            }
            f.close();
            Serial1.print("fs_wrote="); Serial1.println((uint32_t)n);
            Serial1.println("fs_rt=ok");
          }
        } else if (cmd == "fsread") {
          String path = "/" + arg + ".txt";
          File f = FatFS.open(path.c_str(), "r");
          if (!f) { Serial1.println("fs_rt=open_read_failed"); }
          else {
            Serial1.print("fs_rt_size="); Serial1.println((uint32_t)f.size());
            String first = f.readStringUntil('\n');
            f.close();
            String want = arg + " line 0";
            Serial1.print("fs_first_ok=");
            Serial1.println(first == want ? 1 : 0);
            Serial1.println("fs_rt=ok");
          }
        } else if (cmd == "fsexists") {
          String path = "/" + arg + ".txt";
          Serial1.print("fs_exists=");
          Serial1.println(FatFS.exists(path.c_str()) ? 1 : 0);
        } else if (cmd == "fslist") {
          int n = 0;
          Dir d = FatFS.openDir("/");
          while (d.next()) { n++; }
          Serial1.print("fs_entries="); Serial1.println(n);
        } else if (cmd == "ftliszero") {
          ftlCreate();
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
        } else if (cmd == "ftldump") {
          ftlCreate();
          /* Enough of an LBA to tell a FAT boot sector from erased space,
             which is what distinguishes "the mapping came back and the
             filesystem is on it" from "something reformatted underneath". */
          uint8_t buf[512];
          bool ok = ftl->read(arg.toInt(), buf);
          Serial1.print("ftl_read="); Serial1.println(ok ? 1 : 0);
          Serial1.print("ftl_head=");
          for (int i = 0; i < 8; i++) {
            if (buf[i] < 16) { Serial1.print('0'); }
            Serial1.print(buf[i], HEX);
          }
          Serial1.println();
          Serial1.print("ftl_bootsig=");
          Serial1.println((buf[510] == 0x55 && buf[511] == 0xAA) ? 1 : 0);
        } else if (cmd == "ftlheads") {
          ftlCreate();
          /* The first four bytes of each of the low LBAs. Comparing this
             across a reset says exactly WHICH mappings were lost, which a
             pass/fail on one file cannot. */
          int n = arg.toInt(); if (n <= 0 || n > 64) { n = 24; }
          uint8_t buf[512];
          for (int i = 0; i < n; i++) {
            ftl->read(i, buf);
            Serial1.print("lba");
            if (i < 10) { Serial1.print('0'); }
            Serial1.print(i); Serial1.print("=");
            for (int j = 0; j < 4; j++) {
              if (buf[j] < 16) { Serial1.print('0'); }
              Serial1.print(buf[j], HEX);
            }
            Serial1.println();
          }
        } else if (cmd == "ftlstats") {
          ftlCreate();
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
          ftlCreate();
          /* SPIFTL's own consistency check: crosslinked LBAs, empty-block
             accounting, wear spread. It prints its own complaints. */
          Serial1.print("ftl_check=");
          Serial1.println(ftl && ftl->check() ? 1 : 0);
        } else if (cmd == "ftlwrite") {
          ftlCreate();
          ftlWrite(arg.toInt(), 0);
        } else if (cmd == "ftlverify") {
          ftlCreate();
          ftlVerify(arg.toInt(), 0);
        } else if (cmd == "ftlrewrite") {
          ftlCreate();
          ftlWrite(arg.toInt(), 0x5A);
        } else if (cmd == "ftlreverify") {
          ftlCreate();
          ftlVerify(arg.toInt(), 0x5A);
        } else if (cmd == "ftlchurn") {
          ftlCreate();
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
