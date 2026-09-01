/* The filesystem, over the SD block layer.
 *
 * Driven from the console so the host can ask for one thing at a time. Both
 * APIs are exercised -- SDFS for the arduino-pico/esp8266 shape and SD for the
 * classic one -- because they are separate code paths onto the same
 * filesystem, and a shim that compiles is not a shim that works.
 */
#include <Arduino.h>
#include <SD.h>
#include <SDFS.h>
extern "C" {
#include "ch32h4_sdmmc.h"
}

static char line[128];
static int len = 0;

void setup() {
  Serial1.begin(115200);
  Serial1.println("sdfstest starting");
  Serial1.print("> ");
}

static void doMount(uint8_t width, uint32_t freq, bool autoFormat) {
  SDFS.end();
  SDFSConfig cfg(width, freq);
  cfg.setAutoFormat(autoFormat);
  SDFS.setConfig(cfg);

  uint32_t t0 = millis();
  bool ok = SDFS.begin();
  Serial1.print("fs_mount="); Serial1.println(ok ? 1 : 0);
  Serial1.print("fs_mount_ms="); Serial1.println(millis() - t0);
  if (!ok) return;

  FSInfo info;
  if (SDFS.info(info)) {
    Serial1.print("fs_total_kb=");
    Serial1.println((uint32_t)(info.totalBytes / 1024));
    Serial1.print("fs_used_kb=");
    Serial1.println((uint32_t)(info.usedBytes / 1024));
    Serial1.print("fs_cluster=");
    Serial1.println((uint32_t)info.blockSize);
  }
  Serial1.print("card_kb=");
  Serial1.println((uint32_t)(SD.size64() / 1024));
}

static void doFormat() {
  SDFS.end();
  uint32_t t0 = millis();
  bool ok = SDFS.format();
  Serial1.print("fs_format="); Serial1.println(ok ? 1 : 0);
  Serial1.print("fs_format_ms="); Serial1.println(millis() - t0);
}

/* Write a file, read it back, and check every byte. The content is generated
   rather than constant so a short read, a stale buffer or an off-by-one
   cannot pass. */
static void doFileRoundTrip(uint32_t bytes) {
  const char *path = "/rt.bin";
  SDFS.remove(path);

  File f = SDFS.open(path, "w");
  if (!f) { Serial1.println("fs_rt=open_write_failed"); return; }

  uint8_t chunk[256];
  uint32_t written = 0;
  uint32_t t0 = millis();
  while (written < bytes) {
    uint32_t n = bytes - written < sizeof(chunk) ? bytes - written : sizeof(chunk);
    for (uint32_t i = 0; i < n; i++) {
      chunk[i] = (uint8_t)((written + i) * 7u + ((written + i) >> 8));
    }
    size_t w = f.write(chunk, n);
    if (w != n) { Serial1.println("fs_rt=short_write"); f.close(); return; }
    written += n;
  }
  f.close();
  uint32_t wms = millis() - t0;

  f = SDFS.open(path, "r");
  if (!f) { Serial1.println("fs_rt=open_read_failed"); return; }
  Serial1.print("fs_rt_size="); Serial1.println((uint32_t)f.size());

  bool ok = (f.size() == bytes);
  uint32_t pos = 0;
  t0 = millis();
  while (ok && pos < bytes) {
    int got = f.read(chunk, sizeof(chunk));
    if (got <= 0) { ok = false; break; }
    for (int i = 0; i < got; i++) {
      if (chunk[i] != (uint8_t)((pos + i) * 7u + ((pos + i) >> 8))) { ok = false; break; }
    }
    pos += got;
  }
  f.close();
  uint32_t rms = millis() - t0;

  Serial1.print("fs_rt_write_ms="); Serial1.println(wms);
  Serial1.print("fs_rt_read_ms="); Serial1.println(rms);
  Serial1.print("fs_rt="); Serial1.println(ok && pos == bytes ? "ok" : "FAIL");
}

/* Append must extend rather than truncate -- FILE_WRITE in the classic API
   means append, and a shim that maps it to "w" silently eats the log. */
static void doAppend() {
  const char *path = "/ap.txt";
  SD.remove(path);

  for (int i = 0; i < 3; i++) {
    File f = SD.open(path, FILE_WRITE);
    if (!f) { Serial1.println("fs_append=open_failed"); return; }
    f.print("line");
    f.println(i);
    f.close();
  }

  File f = SD.open(path, FILE_READ);
  if (!f) { Serial1.println("fs_append=reopen_failed"); return; }
  String all;
  while (f.available()) all += (char)f.read();
  f.close();

  int lines = 0;
  for (unsigned i = 0; i < all.length(); i++) if (all[i] == '\n') lines++;
  Serial1.print("fs_append_bytes="); Serial1.println(all.length());
  Serial1.print("fs_append_lines="); Serial1.println(lines);
  Serial1.print("fs_append=");
  Serial1.println((lines == 3 && all.indexOf("line0") >= 0
                   && all.indexOf("line2") >= 0) ? "ok" : "FAIL");
}

static void doSeek() {
  const char *path = "/sk.bin";
  SDFS.remove(path);
  File f = SDFS.open(path, "w");
  if (!f) { Serial1.println("fs_seek=open_failed"); return; }
  for (int i = 0; i < 1024; i++) f.write((uint8_t)i);
  f.close();

  f = SDFS.open(path, "r");
  bool ok = f && f.size() == 1024;
  ok = ok && f.seek(500, SeekSet) && f.position() == 500 && f.read() == (500 & 0xFF);
  ok = ok && f.seek(9, SeekCur) && f.position() == 510 && f.read() == (510 & 0xFF);
  ok = ok && f.seek(1, SeekEnd) && f.position() == 1023;
  f.close();
  Serial1.print("fs_seek="); Serial1.println(ok ? "ok" : "FAIL");
}

static void doDirs() {
  /* Including c.txt, which a previous run's rename left behind. A test that
     does not clean up after itself passes once and then reports a stale
     artefact as a failure. */
  SDFS.remove("/d/a.txt");
  SDFS.remove("/d/b.txt");
  SDFS.remove("/d/c.txt");
  SDFS.rmdir("/d");

  bool ok = SDFS.mkdir("/d");
  static const char *const names[] = {"/d/a.txt", "/d/b.txt"};
  for (const char *n : names) {
    File f = SDFS.open(n, "w");
    if (!f) { ok = false; break; }
    f.print("x");
    f.close();
  }

  int count = 0;
  Dir dir = SDFS.openDir("/d");
  while (dir.next()) {
    count++;
    Serial1.print("fs_dir_entry="); Serial1.print(dir.fileName());
    Serial1.print(" size="); Serial1.println((uint32_t)dir.fileSize());
  }
  Serial1.print("fs_dir_count="); Serial1.println(count);

  bool existed = SDFS.exists("/d/a.txt");
  ok = ok && SDFS.rename("/d/a.txt", "/d/c.txt");
  bool renamed = SDFS.exists("/d/c.txt") && !SDFS.exists("/d/a.txt");

  Serial1.print("fs_dir=");
  Serial1.println((ok && count == 2 && existed && renamed) ? "ok" : "FAIL");
}

/* Files must survive an unmount and remount -- that is the whole point of a
   filesystem, and a driver that only ever reads back its own cache passes
   every test above without it. */
static void doPersist() {
  const char *path = "/pv.txt";
  SDFS.remove(path);
  File f = SDFS.open(path, "w");
  if (!f) { Serial1.println("fs_persist=open_failed"); return; }
  f.print("persisted-42");
  f.close();

  SDFS.end();
  if (!SDFS.begin()) { Serial1.println("fs_persist=remount_failed"); return; }

  f = SDFS.open(path, "r");
  String s;
  if (f) { while (f.available()) s += (char)f.read(); f.close(); }
  Serial1.print("fs_persist_read="); Serial1.println(s);
  Serial1.print("fs_persist=");
  Serial1.println(s == "persisted-42" ? "ok" : "FAIL");
}

static void handle(char *cmd) {
  if (!strncmp(cmd, "fsmount", 7)) {
    uint8_t width = 1;
    uint32_t freq = 20000000;
    bool autoFormat = strstr(cmd, "autoformat") != nullptr;
    char *sp = strchr(cmd, ' ');
    if (sp && sp[1] >= '0' && sp[1] <= '9') {
      width = (uint8_t)atoi(sp + 1);
      char *sp2 = strchr(sp + 1, ' ');
      if (sp2 && sp2[1] >= '0' && sp2[1] <= '9') freq = (uint32_t)atol(sp2 + 1);
    }
    doMount(width, freq, autoFormat);

  } else if (!strcmp(cmd, "fsformat")) {
    doFormat();
  } else if (!strncmp(cmd, "fsrt ", 5)) {
    doFileRoundTrip((uint32_t)atol(cmd + 5));
  } else if (!strcmp(cmd, "fsappend")) {
    doAppend();
  } else if (!strcmp(cmd, "fsseek")) {
    doSeek();
  } else if (!strcmp(cmd, "fsdirs")) {
    doDirs();
  } else if (!strcmp(cmd, "fspersist")) {
    doPersist();
  } else if (!strcmp(cmd, "sdtrace")) {
    Serial1.print("trace_n="); Serial1.println(ch32h4_sd_log_n);
    for (uint8_t i = 0; i < ch32h4_sd_log_n; i++) {
      Serial1.print("trace CMD"); Serial1.print(ch32h4_sd_log[i].cmd);
      Serial1.print(" fg=0x"); Serial1.print(ch32h4_sd_log[i].flags, HEX);
      Serial1.print(" r=0x"); Serial1.println(ch32h4_sd_log[i].resp, HEX);
    }

  } else if (!strcmp(cmd, "sdraw")) {
    Serial1.print("sd_begin="); Serial1.println(ch32h4_sd_begin(1, 20000000));
    Serial1.print("sd_ready="); Serial1.println(ch32h4_sd_ready() ? 1 : 0);

  } else if (!strcmp(cmd, "sdregs")) {
    static const char *n[] = {"CONTROL","CLK_DIV","STATUS","INT_FG","HBRSTR","HBPCENR"};
    for (uint8_t i = 0; i < 6; i++) {
      Serial1.print("reg_"); Serial1.print(n[i]); Serial1.print("=0x");
      Serial1.println(ch32h4_sd_debug(i), HEX);
    }

  } else if (!strcmp(cmd, "sdrawend")) {
    ch32h4_sd_end();
    Serial1.print("sd_ready="); Serial1.println(ch32h4_sd_ready() ? 1 : 0);

  } else if (!strcmp(cmd, "fsheap")) {
    Serial1.print("heap_free="); Serial1.println((uint32_t)ch32h4_heap_free());
  }
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
