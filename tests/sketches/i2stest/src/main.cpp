/* I2S out, to whatever DAC or amplifier is on PB12/PB13/PB15.
 *
 * The measurements that can be made without an oscilloscope are the rate the
 * divider actually produced, and the throughput of the transmit path -- if the
 * DMA is consuming the ring at the sample rate, then the clock, the pins, the
 * peripheral and the DMA are all doing their jobs. Whether it *sounds* right
 * needs ears or a scope, so the tone commands exist for that.
 */
#include <Arduino.h>
#include <I2S.h>

/* Heap-allocated, so the instance and the direction can both be changed at
   run time -- there are two I2S blocks and only one of them is wired to the
   amplifier, and a test that can only ever construct the wired one cannot say
   anything about the other. */
static I2S *i2sp = new I2S(OUTPUT, 0);
static uint8_t inst = 0;
static bool is_rx = false;

static char line[96];
static int len = 0;
static bool started = false;

#define i2s (*i2sp)

static void doInstance(uint8_t id, bool rx) {
  if (started) { i2s.end(); started = false; }
  delete i2sp;
  inst = id > 1 ? 0 : id;
  is_rx = rx;
  i2sp = new I2S(rx ? INPUT : OUTPUT, inst);
  Serial1.print("i2s_instance="); Serial1.println(inst);
  Serial1.print("i2s_rx="); Serial1.println(is_rx ? 1 : 0);
  /* The pins the object actually adopted, not the ones the variant lists for
     instance 0 -- which is exactly the bug this reports on. */
  Serial1.print("i2s_pin_ck="); Serial1.println(inst ? PIN_I2S2_CK : PIN_I2S1_CK);
  Serial1.print("i2s_pin_ws="); Serial1.println(inst ? PIN_I2S2_WS : PIN_I2S1_WS);
  Serial1.print("i2s_pin_sd="); Serial1.println(inst ? PIN_I2S2_SD : PIN_I2S1_SD);
  Serial1.print("i2s_af_ck="); Serial1.println(inst ? PIN_I2S2_AF_CK : PIN_I2S1_AF_CK);
  Serial1.print("i2s_af_sd="); Serial1.println(inst ? PIN_I2S2_AF_SD : PIN_I2S1_AF_SD);
  /* setBCLK is the observable check that the object took its OWN instance's
     pins: it accepts that instance's clock pin and refuses the other's. */
  Serial1.print("i2s_accepts_own_ck=");
  Serial1.println(i2s.setBCLK(inst ? PIN_I2S2_CK : PIN_I2S1_CK) ? 1 : 0);
  Serial1.print("i2s_accepts_other_ck=");
  Serial1.println(i2s.setBCLK(inst ? PIN_I2S1_CK : PIN_I2S2_CK) ? 1 : 0);
}

static void doBegin(uint32_t rate, int bits) {
  if (started) {
    i2s.end();
    started = false;
  }
  i2s.setBitsPerSample(bits);
  i2s.setBuffer(8192);
  uint32_t t0 = micros();
  bool ok = i2s.begin(rate);
  uint32_t us = micros() - t0;

  Serial1.print("i2s_begin="); Serial1.println(ok ? 1 : 0);
  Serial1.print("i2s_begin_us="); Serial1.println(us);
  if (!ok) return;
  started = true;

  Serial1.print("i2s_rate_requested="); Serial1.println(rate);
  Serial1.print("i2s_rate_actual="); Serial1.println(i2s.actualFrequency());
  /* Parts per million, so a divider that is close is distinguishable from one
     that is merely plausible. */
  int32_t err = (int32_t)i2s.actualFrequency() - (int32_t)rate;
  Serial1.print("i2s_rate_err_ppm=");
  Serial1.println((int32_t)((int64_t)err * 1000000 / (int32_t)rate));
  Serial1.print("i2s_bits="); Serial1.println(bits);
  Serial1.print("i2s_avail_write="); Serial1.println(i2s.availableForWrite());
  Serial1.print("i2s_rate_min="); Serial1.println(i2s.minimumFrequency());
  Serial1.print("i2s_rate_max="); Serial1.println(i2s.maximumFrequency());
}

/* Feed a sine for N milliseconds and report how many frames actually went out.
 * The frame count against elapsed time is the sample rate, measured from the
 * outside -- which is the check that the DMA is really clocking data at the
 * rate the divider claims.
 *
 * THE AMPLITUDE IS AN EXPLICIT ARGUMENT AND DEFAULTS TO ZERO.
 *
 * Everything this command measures -- the divider, the DMA throughput, the
 * underflow count -- is measured just as well by clocking silence, because
 * none of it depends on the sample values. There is a real amplifier and a
 * real speaker on the other end of these pins, and a test that makes a noise
 * by default will eventually make it at three in the morning. Audible output
 * has to be asked for, by number, every time. */
static void doTone(uint32_t hz, uint32_t ms, int16_t amplitude) {
  if (!started) { Serial1.println("i2s_tone=not_started"); return; }

  i2s.getUnderflows();
  const uint32_t rate = i2s.actualFrequency();
  uint32_t frames = 0;
  uint32_t phase = 0;
  /* Fixed point: a full turn is 2^16. */
  const uint32_t step = (uint32_t)(((uint64_t)hz << 16) / rate);

  /* A full-scale turn, scaled down to the requested amplitude below. */
  static const int16_t sine[16] = {
        0,  12539,  23170,  30273,  32767,  30273,  23170,  12539,
        0, -12539, -23170, -30273, -32767, -30273, -23170, -12539
  };

  const uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    /* Batch, so the per-call overhead does not dominate the measurement. */
    int16_t buf[64];
    for (int i = 0; i < 32; i++) {
      int16_t s = (int16_t)(((int32_t)sine[(phase >> 12) & 0x0F]
                             * amplitude) >> 15);
      buf[i * 2] = s;
      buf[i * 2 + 1] = s;
      phase += step;
    }
    size_t n = i2s.write((const uint8_t *)buf, sizeof(buf));
    frames += n / 4;
    yield();
  }
  const uint32_t elapsed = millis() - t0;

  Serial1.print("i2s_tone_hz="); Serial1.println(hz);
  Serial1.print("i2s_frames="); Serial1.println(frames);
  Serial1.print("i2s_ms="); Serial1.println(elapsed);
  /* Frames WRITTEN includes everything still sitting in the ring, which on a
     run that starts empty is a whole bufferful the wire has not seen. Subtract
     it, or a short run reports a rate several percent high and the divider
     gets blamed for the buffer. */
  const uint32_t queued = (uint32_t)((8192 - i2s.availableForWrite()) / 4);
  const uint32_t sent = frames > queued ? frames - queued : 0;
  Serial1.print("i2s_queued="); Serial1.println(queued);
  Serial1.print("i2s_measured_rate=");
  Serial1.println(elapsed ? (uint32_t)((uint64_t)sent * 1000 / elapsed) : 0);
  Serial1.print("i2s_amplitude="); Serial1.println(amplitude);
  Serial1.print("i2s_underflows="); Serial1.println(i2s.getUnderflows());
}

/* Silence, at the same rate. Proves the transmit path is clocking even with
   nothing interesting in it, and leaves the amplifier quiet afterwards. */
static void doSilence(uint32_t ms) {
  if (!started) { Serial1.println("i2s_silence=not_started"); return; }
  i2s.getUnderflows();
  int16_t buf[64] = {0};
  uint32_t frames = 0;
  const uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    frames += i2s.write((const uint8_t *)buf, sizeof(buf)) / 4;
    yield();
  }
  const uint32_t elapsed = millis() - t0;
  /* Subtract what is still in the ring, for the same reason doTone() does:
     frames WRITTEN includes a whole bufferful the wire has never seen, and
     over a short run that reads as a rate several percent high. Leaving it out
     here made this command disagree with doTone() by 2.2% on the same
     divider. */
  const uint32_t queued = (uint32_t)((8192 - i2s.availableForWrite()) / 4);
  const uint32_t sent = frames > queued ? frames - queued : 0;
  Serial1.print("i2s_frames="); Serial1.println(frames);
  Serial1.print("i2s_queued="); Serial1.println(queued);
  Serial1.print("i2s_measured_rate=");
  Serial1.println(elapsed ? (uint64_t)sent * 1000 / elapsed : 0);
  Serial1.print("i2s_underflows="); Serial1.println(i2s.getUnderflows());
}

static void handle(char *cmd) {
  if (!strncmp(cmd, "i2sbegin", 8)) {
    uint32_t rate = 44100;
    int bits = 16;
    char *sp = strchr(cmd, ' ');
    if (sp) {
      rate = (uint32_t)atol(sp + 1);
      char *sp2 = strchr(sp + 1, ' ');
      if (sp2) bits = atoi(sp2 + 1);
    }
    doBegin(rate, bits);

  } else if (!strncmp(cmd, "i2stone ", 8)) {
    /* i2stone <hz> [ms] [amplitude]
       Amplitude is 0..32767 and defaults to ZERO -- silent. Everything this
       measures works the same at any amplitude, so making a noise is opt-in.
       See the comment on doTone(). */
    uint32_t hz = (uint32_t)atol(cmd + 8);
    uint32_t ms = 500;
    int16_t amp = 0;
    char *sp = strchr(cmd + 8, ' ');
    if (sp) {
      ms = (uint32_t)atol(sp + 1);
      char *sp2 = strchr(sp + 1, ' ');
      if (sp2) {
        long a = atol(sp2 + 1);
        amp = (int16_t)(a < 0 ? 0 : (a > 32767 ? 32767 : a));
      }
    }
    doTone(hz, ms, amp);

  } else if (!strncmp(cmd, "i2ssilence ", 11)) {
    doSilence((uint32_t)atol(cmd + 11));

  } else if (!strcmp(cmd, "i2send")) {
    Serial1.print("i2s_end="); Serial1.println(i2s.end() ? 1 : 0);
    started = false;

  } else if (!strcmp(cmd, "i2sstat")) {
    Serial1.print("i2s_started="); Serial1.println(started ? 1 : 0);
    Serial1.print("i2s_rate_actual="); Serial1.println(i2s.actualFrequency());
    Serial1.print("i2s_avail_write="); Serial1.println(i2s.availableForWrite());
    Serial1.print("i2s_underflows="); Serial1.println(i2s.getUnderflows());

  } else if (!strncmp(cmd, "i2sinst ", 8)) {
    /* i2sinst <0|1> [rx] */
    uint8_t id = (uint8_t)atoi(cmd + 8);
    char *sp = strchr(cmd + 8, ' ');
    doInstance(id, sp && atoi(sp + 1));

  } else if (!strcmp(cmd, "i2spins")) {
    Serial1.print("pin_ck="); Serial1.println(PIN_I2S_CK);
    Serial1.print("pin_ws="); Serial1.println(PIN_I2S_WS);
    Serial1.print("pin_sd="); Serial1.println(PIN_I2S_SD);
    Serial1.print("i2s1_ck="); Serial1.println(PIN_I2S1_CK);
    Serial1.print("i2s2_ck="); Serial1.println(PIN_I2S2_CK);
    Serial1.print("i2s2_sd="); Serial1.println(PIN_I2S2_SD);
    Serial1.print("i2s2_ws="); Serial1.println(PIN_I2S2_WS);
    Serial1.print("i2s_instance="); Serial1.println(inst);
    Serial1.print("vio18_sel="); Serial1.println((PWR->CTLR >> 10) & 0x7);
  }
  Serial1.print("> ");
}

void setup() {
  Serial1.begin(115200);
  Serial1.println("i2stest starting");
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
