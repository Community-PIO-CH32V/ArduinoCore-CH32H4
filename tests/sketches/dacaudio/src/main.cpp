/* The DAC streaming path, on Serial1.
 *
 * SILENT BY DESIGN. Every command here writes mid-scale or reads counters.
 * Nothing on PA4/PA5 reaches the bench amplifier, which is on I2S's pins, but
 * the rule holds regardless: no command in this sketch generates a waveform.
 */
#include <Arduino.h>
#include <DACAudio.h>

extern "C" {
#include "ch32h4_timer.h"
}

static DACAudio dac;

static char line[64];
static int len = 0;

static void handle(const char *cmd) {
  if (!strncmp(cmd, "rate ", 5)) {
    dac.end();
    bool ok = dac.begin((uint32_t)atol(cmd + 5));
    Serial1.print("begun="); Serial1.println(ok ? 1 : 0);
    Serial1.print("rate="); Serial1.println(dac.sampleRate());
    Serial1.print("frames="); Serial1.println((uint32_t)dac.bufferFrames());

  } else if (!strcmp(cmd, "beginagain")) {
    /* A second begin() without an end() must be refused rather than
       reconfiguring hardware out from under a running stream. */
    Serial1.print("begun="); Serial1.println(dac.begin(44100) ? 1 : 0);

  } else if (!strcmp(cmd, "stop")) {
    dac.end();
    Serial1.print("running="); Serial1.println(dac.running() ? 1 : 0);

  } else if (!strncmp(cmd, "measure ", 8)) {
    /* The DMA consumes exactly one word per conversion, so counting how far it
       moves over a known interval measures the true sample rate. The ring
       wraps, so the difference is taken modulo its length -- and the caller
       must pick a window shorter than one pass or the wrap is invisible. */
    uint32_t ms = (uint32_t)atol(cmd + 8);
    if (ms < 5) { ms = 200; }
    size_t a = dac.dmaPosition();
    uint32_t t0 = micros();
    delay(ms);
    size_t b = dac.dmaPosition();
    uint32_t el = micros() - t0;
    size_t moved = (b + dac.bufferFrames() - a) % dac.bufferFrames();
    Serial1.print("moved="); Serial1.println((uint32_t)moved);
    Serial1.print("elapsed_us="); Serial1.println(el);
    Serial1.print("measured_hz=");
    Serial1.println((uint32_t)(((uint64_t)moved * 1000000u) / el));

  } else if (!strcmp(cmd, "timerowner")) {
    Serial1.print("owner=");
    Serial1.println(ch32h4_timer_owner_name(ch32h4_timer_owner(6)));
    /* CEN. Releasing the claim is bookkeeping; leaving the counter running
       would keep raising TRGO at whoever configures the DAC next. */
    Serial1.print("tim6_enabled="); Serial1.println(TIM6->CTLR1 & 0x1u);

  } else if (!strncmp(cmd, "drain ", 6)) {
    /* Queue some frames, then stop writing and let the DMA eat them. Free
       space must GROW, which is the half of the accounting that a writer-only
       test never exercises. */
    uint32_t n = (uint32_t)atol(cmd + 6);
    for (uint32_t i = 0; i < n; i++) {
      dac.writeFrame(0, 0);
    }
    uint32_t a = (uint32_t)dac.availableFrames();
    delay(20);
    uint32_t b = (uint32_t)dac.availableFrames();
    Serial1.print("free_before="); Serial1.println(a);
    Serial1.print("free_after="); Serial1.println(b);

  } else if (!strcmp(cmd, "avail")) {
    Serial1.print("avail="); Serial1.println((uint32_t)dac.availableFrames());

  } else if (!strncmp(cmd, "fill ", 5)) {
    /* Mid-scale frames: silent, and enough to move the ring pointers. */
    uint32_t n = (uint32_t)atol(cmd + 5);
    uint32_t wrote = 0;
    for (uint32_t i = 0; i < n; i++) {
      wrote += (uint32_t)dac.writeFrame(0, 0);
    }
    Serial1.print("wrote="); Serial1.println(wrote);
    Serial1.print("underruns="); Serial1.println(dac.underruns());

  } else if (!strncmp(cmd, "feed ", 5)) {
    /* Keep the ring fed for the given milliseconds, which is what a healthy
       producer does; underruns must stay at zero throughout. */
    uint32_t ms = (uint32_t)atol(cmd + 5);
    uint32_t t0 = millis();
    while (millis() - t0 < ms) {
      while (dac.availableFrames() > 0) {
        dac.writeFrame(0, 0);
      }
      delay(1);
    }
    Serial1.print("underruns="); Serial1.println(dac.underruns());

  } else if (!strncmp(cmd, "starve ", 7)) {
    /* Stop writing for long enough that the DMA empties the ring, then write
       once so the check runs. */
    delay((uint32_t)atol(cmd + 7));
    dac.writeFrame(0, 0);
    Serial1.print("underruns="); Serial1.println(dac.underruns());

  } else if (!strcmp(cmd, "monocheck") || !strcmp(cmd, "stereocheck")) {
    /* Left and right are deliberately opposite quarter-scale values, so mono
       and stereo produce DIFFERENT register contents: mono duplicates left
       into both halves (0xA00, 0xA00) and stereo does not (0xA00, 0x600).
       Checking only that the halves match would pass on silence, which is
       0x800 in both halves and is what a broken write leaves behind.

       The WHOLE ring is filled, not one frame. A single frame is played and
       gone within one sample period, so a read a few milliseconds later
       catches the silence that follows it and proves nothing. */
    dac.setMono(cmd[0] == 'm');
    for (int pass = 0; pass < 3; pass++) {
      while (dac.availableFrames() > 0) {
        dac.writeFrame(8192, -8192);
      }
    }
    delay(2);
    /* DOR, not the holding register: DOR is what the DAC is actually
       converting, which is the question. RD12BDHR is reported alongside only
       to show what it does with a DMA writing through it. */
    uint32_t l = DAC_GetDataOutputValue(DAC_Channel_1);
    uint32_t r = DAC_GetDataOutputValue(DAC_Channel_2);
    uint32_t v = DAC->RD12BDHR;
    dac.setStereo(true);
    Serial1.print("left="); Serial1.println(l & 0xFFFu);
    Serial1.print("right="); Serial1.println(r & 0xFFFu);
    Serial1.print("dhr_left="); Serial1.println(v & 0xFFFu);
    Serial1.print("dhr_right="); Serial1.println((v >> 16) & 0xFFFu);
    Serial1.print("free="); Serial1.println((uint32_t)dac.availableFrames());
    /* What the CPU sees at the word the DMA is reading, against what the DAC
       actually converted. If these disagree the ring is fine and the transfer
       is not -- unreachable memory, or a stale view of it. */
    const uint32_t *buf = dac.buffer();
    if (!buf) {
      Serial1.println("ring_addr=0");   /* not running; nothing to inspect */
      Serial1.print("> ");
      return;
    }
    size_t rd = dac.dmaPosition();
    Serial1.print("ring_addr=0x"); Serial1.println((uint32_t)buf, HEX);
    Serial1.print("ring_at_rd=0x"); Serial1.println(buf[rd], HEX);
    Serial1.print("ring_rd="); Serial1.println((uint32_t)rd);
    /* How much of the ring actually holds samples rather than silence. If this
       is near zero the writes are being overwritten, not lost. */
    uint32_t nonsilent = 0, first = 0xFFFFFFFFu;
    for (size_t i = 0; i < dac.bufferFrames(); i++) {
      if (buf[i] != 0x08000800u) {
        if (!nonsilent) { first = (uint32_t)i; }
        nonsilent++;
      }
    }
    Serial1.print("ring_nonsilent="); Serial1.println(nonsilent);
    Serial1.print("ring_first="); Serial1.println(first);

  } else {
    Serial1.print("unknown: "); Serial1.println(cmd);
  }
  Serial1.print("> ");
}

void setup() {
  Serial1.begin(115200);
  Serial1.println();
  Serial1.println("dacaudio starting");
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
