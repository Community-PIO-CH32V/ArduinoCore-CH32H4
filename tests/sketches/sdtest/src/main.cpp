/* The SD card block layer, on the SDMMC controller.
 *
 * No filesystem here on purpose. Almost every way this can fail fails in the
 * block driver -- identification, the command gap, the DMA arming asymmetry,
 * the bounce buffers -- and a FatFS layer on top would turn all of those into
 * one "mount failed".
 *
 * Every write test is NON-DESTRUCTIVE: it reads the block first, writes its
 * pattern, verifies, and puts the original contents back. A card wired to a
 * dev board usually has something on it, and a test that eats a partition
 * table to prove a driver works is not a good trade.
 */
#include <Arduino.h>
extern "C" {
#include "ch32h4_sdmmc.h"
}

static char line[96];
static int len = 0;

static uint8_t buf[8 * CH32H4_SD_BLOCK_SIZE];
static uint8_t saved[8 * CH32H4_SD_BLOCK_SIZE];

static const char *errName(int e) {
  switch (e) {
    case CH32H4_SD_OK:        return "ok";
    case CH32H4_SD_ETIMEDOUT: return "timeout";
    case CH32H4_SD_EIO:       return "io";
    case CH32H4_SD_ENODEV:    return "nodev";
    case CH32H4_SD_ERANGE:    return "range";
    case CH32H4_SD_EVOLTAGE:  return "vio18_too_low";
    case CH32H4_SD_EPARAM:    return "param";
    default:                  return "unknown";
  }
}

static uint32_t sum32(const uint8_t *p, size_t n) {
  /* FNV-1a. A plain sum would not notice two blocks swapped, which is exactly
     the failure the alternating bounce buffers exist to prevent. */
  uint32_t h = 2166136261u;
  for (size_t i = 0; i < n; i++) {
    h ^= p[i];
    h *= 16777619u;
  }
  return h;
}

static void printHex128(const uint32_t v[4]) {
  for (int i = 0; i < 4; i++) {
    for (int s = 28; s >= 0; s -= 4) {
      Serial1.print("0123456789abcdef"[(v[i] >> s) & 0xF]);
    }
  }
}

void setup() {
  Serial1.begin(115200);
  Serial1.println("sdtest starting");
  Serial1.print("> ");
}

static void doInit(uint8_t width, uint32_t freq) {
  uint32_t t0 = micros();
  int e = ch32h4_sd_begin(width, freq);
  uint32_t us = micros() - t0;

  Serial1.print("sd_init="); Serial1.println(errName(e));
  Serial1.print("sd_init_us="); Serial1.println(us);
  if (e != 0) return;

  Serial1.print("sd_type=");
  Serial1.println(ch32h4_sd.card_type == CH32H4_SD_CARD_SDHC ? "SDHC" : "SDSC");
  Serial1.print("sd_width="); Serial1.println(ch32h4_sd.width);
  Serial1.print("sd_freq="); Serial1.println(ch32h4_sd.freq);
  Serial1.print("sd_high_speed="); Serial1.println(ch32h4_sd.high_speed ? 1 : 0);
  Serial1.print("sd_rca=0x"); Serial1.println(ch32h4_sd.rca, HEX);
  Serial1.print("sd_blocks="); Serial1.println(ch32h4_sd.block_count);
  Serial1.print("sd_mb="); Serial1.println(ch32h4_sd.block_count / 2048);
  Serial1.print("sd_cid="); printHex128(ch32h4_sd.cid); Serial1.println();
  Serial1.print("sd_csd="); printHex128(ch32h4_sd.csd); Serial1.println();
}

static void doRead(uint32_t block, uint32_t n) {
  if (n > 8) n = 8;
  memset(buf, 0xA5, n * CH32H4_SD_BLOCK_SIZE);
  uint32_t t0 = micros();
  int e = ch32h4_sd_read_blocks(block, buf, n);
  uint32_t us = micros() - t0;

  Serial1.print("sd_read="); Serial1.println(errName(e));
  if (e != 0) return;
  Serial1.print("sd_read_us="); Serial1.println(us);
  Serial1.print("sd_read_sum=0x");
  Serial1.println(sum32(buf, n * CH32H4_SD_BLOCK_SIZE), HEX);
  /* The first sixteen bytes, so a host can eyeball a boot sector. */
  Serial1.print("sd_read_head=");
  for (int i = 0; i < 16; i++) {
    Serial1.print("0123456789abcdef"[buf[i] >> 4]);
    Serial1.print("0123456789abcdef"[buf[i] & 0xF]);
  }
  Serial1.println();
}

/* Read, overwrite with a pattern, verify, restore, verify the restore. */
static void doWriteVerify(uint32_t block, uint32_t n) {
  if (n > 8) n = 8;
  const uint32_t bytes = n * CH32H4_SD_BLOCK_SIZE;

  int e = ch32h4_sd_read_blocks(block, saved, n);
  if (e != 0) {
    Serial1.print("sd_wv=save_failed:"); Serial1.println(errName(e));
    return;
  }
  const uint32_t savedSum = sum32(saved, bytes);

  /* A pattern that differs in every block and every position, so a transfer
     that returns the same block twice, or drops one, or is off by a block,
     all show up as a mismatch rather than as a plausible result. */
  for (uint32_t i = 0; i < bytes; i++) {
    buf[i] = (uint8_t)(i * 31u + block * 7u + (i / CH32H4_SD_BLOCK_SIZE) * 101u);
  }
  const uint32_t wantSum = sum32(buf, bytes);

  uint32_t t0 = micros();
  e = ch32h4_sd_write_blocks(block, buf, n);
  uint32_t wus = micros() - t0;
  if (e != 0) {
    Serial1.print("sd_wv=write_failed:"); Serial1.println(errName(e));
    ch32h4_sd_write_blocks(block, saved, n);   /* try to put it back anyway */
    return;
  }

  memset(buf, 0, bytes);
  t0 = micros();
  e = ch32h4_sd_read_blocks(block, buf, n);
  uint32_t rus = micros() - t0;
  const uint32_t gotSum = sum32(buf, bytes);

  /* Restore before reporting, so a failed assertion on the host still leaves
     the card as it was found. */
  int re = ch32h4_sd_write_blocks(block, saved, n);
  memset(buf, 0, bytes);
  int rre = ch32h4_sd_read_blocks(block, buf, n);
  const uint32_t restoredSum = sum32(buf, bytes);

  Serial1.print("sd_wv_write_us="); Serial1.println(wus);
  Serial1.print("sd_wv_read_us="); Serial1.println(rus);
  Serial1.print("sd_wv_match=");
  Serial1.println((e == 0 && gotSum == wantSum) ? 1 : 0);
  Serial1.print("sd_wv_restored=");
  Serial1.println((re == 0 && rre == 0 && restoredSum == savedSum) ? 1 : 0);
  Serial1.print("sd_wv=");
  Serial1.println((e == 0 && gotSum == wantSum && re == 0
                   && restoredSum == savedSum) ? "ok" : "FAIL");
}

/* Reading the same block twice must give the same answer. This is the test
   that catches the DMA_BEG1 reload trap: writing the register with the value
   it already holds starts a transfer without rewinding the pointer, so the
   second read returns whatever followed the first one. */
static void doRepeat(uint32_t block) {
  uint32_t sums[3];
  for (int i = 0; i < 3; i++) {
    memset(buf, (uint8_t)(0x11 * i), CH32H4_SD_BLOCK_SIZE);
    int e = ch32h4_sd_read_blocks(block, buf, 1);
    if (e != 0) {
      Serial1.print("sd_repeat=failed:"); Serial1.println(errName(e));
      return;
    }
    sums[i] = sum32(buf, CH32H4_SD_BLOCK_SIZE);
  }
  Serial1.print("sd_repeat_sums=0x"); Serial1.print(sums[0], HEX);
  Serial1.print(",0x"); Serial1.print(sums[1], HEX);
  Serial1.print(",0x"); Serial1.println(sums[2], HEX);
  Serial1.print("sd_repeat=");
  Serial1.println((sums[0] == sums[1] && sums[1] == sums[2]) ? "ok" : "FAIL");
}

/* A run longer than the driver's eight-block bounce buffer, so the chunking
   loop and the alternating buffers are both exercised for real. Destructive by
   design -- the card under test is scratch. */
static void doBulk(uint32_t block, uint32_t nblocks) {
  const uint32_t CHUNK = 8;
  uint32_t written = 0, us_w = 0, us_r = 0;
  bool ok = true;

  for (uint32_t done = 0; done < nblocks && ok; done += CHUNK) {
    uint32_t n = nblocks - done < CHUNK ? nblocks - done : CHUNK;
    for (uint32_t i = 0; i < n * CH32H4_SD_BLOCK_SIZE; i++) {
      uint32_t abs = (done + i / CH32H4_SD_BLOCK_SIZE);
      buf[i] = (uint8_t)(abs * 37u + i * 13u + 0x5A);
    }
    uint32_t t0 = micros();
    if (ch32h4_sd_write_blocks(block + done, buf, n) != 0) { ok = false; break; }
    us_w += micros() - t0;
    written += n;
  }

  for (uint32_t done = 0; done < written && ok; done += CHUNK) {
    uint32_t n = written - done < CHUNK ? written - done : CHUNK;
    memset(buf, 0, n * CH32H4_SD_BLOCK_SIZE);
    uint32_t t0 = micros();
    if (ch32h4_sd_read_blocks(block + done, buf, n) != 0) { ok = false; break; }
    us_r += micros() - t0;
    for (uint32_t i = 0; i < n * CH32H4_SD_BLOCK_SIZE && ok; i++) {
      uint32_t abs = (done + i / CH32H4_SD_BLOCK_SIZE);
      if (buf[i] != (uint8_t)(abs * 37u + i * 13u + 0x5A)) ok = false;
    }
  }

  Serial1.print("sd_bulk_blocks="); Serial1.println(written);
  Serial1.print("sd_bulk_write_us="); Serial1.println(us_w);
  Serial1.print("sd_bulk_read_us="); Serial1.println(us_r);
  if (us_w) {
    Serial1.print("sd_bulk_write_kbs=");
    Serial1.println((uint32_t)((uint64_t)written * 512ull * 1000ull / us_w));
  }
  if (us_r) {
    Serial1.print("sd_bulk_read_kbs=");
    Serial1.println((uint32_t)((uint64_t)written * 512ull * 1000ull / us_r));
  }
  Serial1.print("sd_bulk="); Serial1.println(ok ? "ok" : "FAIL");
}

static void handle(char *cmd) {
  if (!strncmp(cmd, "sdinit", 6)) {
    uint8_t width = 1;
    uint32_t freq = 20000000;
    char *sp = strchr(cmd, ' ');
    if (sp) {
      width = (uint8_t)atoi(sp + 1);
      char *sp2 = strchr(sp + 1, ' ');
      if (sp2) freq = (uint32_t)atol(sp2 + 1);
    }
    doInit(width, freq);

  } else if (!strncmp(cmd, "sdread ", 7)) {
    char *sp = strchr(cmd + 7, ' ');
    uint32_t n = sp ? (uint32_t)atol(sp + 1) : 1;
    doRead((uint32_t)atol(cmd + 7), n ? n : 1);

  } else if (!strncmp(cmd, "sdwv ", 5)) {
    char *sp = strchr(cmd + 5, ' ');
    uint32_t n = sp ? (uint32_t)atol(sp + 1) : 1;
    doWriteVerify((uint32_t)atol(cmd + 5), n ? n : 1);

  } else if (!strncmp(cmd, "sdbulk ", 7)) {
    char *sp = strchr(cmd + 7, ' ');
    uint32_t n = sp ? (uint32_t)atol(sp + 1) : 64;
    doBulk((uint32_t)atol(cmd + 7), n ? n : 64);

  } else if (!strncmp(cmd, "sdrepeat ", 9)) {
    doRepeat((uint32_t)atol(cmd + 9));

  } else if (!strcmp(cmd, "sdend")) {
    ch32h4_sd_end();
    Serial1.print("sd_ready="); Serial1.println(ch32h4_sd_ready() ? 1 : 0);

  } else if (!strcmp(cmd, "sdstat")) {
    Serial1.print("sd_ready="); Serial1.println(ch32h4_sd_ready() ? 1 : 0);
    Serial1.print("sd_blocks="); Serial1.println(ch32h4_sd.block_count);
    Serial1.print("sd_freq="); Serial1.println(ch32h4_sd.freq);

  } else if (!strcmp(cmd, "vio18")) {
    /* PWR_CTLR bits [12:10]: 0=1.2V 1=1.8V 2=2.5V 3=3.3V. Every SDMMC pin is
       on this rail, so it is the first thing to check when nothing answers. */
    Serial1.print("vio18_sel="); Serial1.println((PWR->CTLR >> 10) & 0x7);
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
