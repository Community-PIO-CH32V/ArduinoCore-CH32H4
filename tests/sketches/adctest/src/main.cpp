/* Timer-paced ADC capture.
 *
 * What can be checked without a signal generator: the rate the timer actually
 * produced, that the DMA delivers samples at that rate measured against the
 * host's clock, that a multi-pin scan keeps its channels in order, and that
 * the values are in range. Whether the ADC is ACCURATE needs a known input,
 * so `adcvref` reads the internal 1.2 V reference, which is the one input the
 * board has that does not need wiring.
 */
#include <Arduino.h>
#include <ADCInput.h>

static ADCInput *adc = nullptr;
static char line[96];
static int len = 0;

/* A scan is named by a string of one-character input codes, so a test can ask
   for a mix of pins and internal channels without a command per combination:
   "0"=A0, "1"=A1, "t"=ATEMP, "v"=AVREF. */
static pin_size_t inputFor(char c) {
  switch (c) {
    case '0': return A0;
    case '1': return A1;
    case '2': return A2;
    case '3': return A3;
    case 't': return ATEMP;
    case 'v': return AVREF;
    default:  return 0xFF;
  }
}

static void doBegin(uint32_t rate, const char *spec) {
  delete adc;
  adc = nullptr;

  pin_size_t p[8];
  int n = 0;
  for (const char *c = spec; *c && n < 8; c++) {
    pin_size_t pin = inputFor(*c);
    if (pin != 0xFF) p[n++] = pin;
  }
  if (n == 0) { p[0] = A0; n = 1; }
  for (int i = n; i < 8; i++) p[i] = 0xFF;

  adc = new ADCInput(p[0], p[1], p[2], p[3], p[4], p[5], p[6], p[7]);
  if (!adc) { Serial1.println("adc_begin=alloc_failed"); return; }
  adc->setBuffer(4096);

  const int nchannels = n;
  Serial1.print("adc_rate_max="); Serial1.println(adc->maximumFrequency());

  bool ok = adc->begin(rate);
  Serial1.print("adc_begin="); Serial1.println(ok ? 1 : 0);
  if (!ok) return;

  Serial1.print("adc_channels="); Serial1.println(nchannels);
  Serial1.print("adc_rate_requested="); Serial1.println(rate);
  Serial1.print("adc_rate_actual="); Serial1.println(adc->actualFrequency());
  int32_t err = (int32_t)adc->actualFrequency() - (int32_t)rate;
  Serial1.print("adc_rate_err_ppm=");
  Serial1.println((int32_t)((int64_t)err * 1000000 / (int32_t)rate));
}

/* Drain for N milliseconds and report the throughput, measured against
   millis() rather than against the timer that is producing it.

   Statistics are kept PER CHANNEL. With a two-pin scan the samples interleave,
   so a single min/max over everything cannot tell a correct capture from one
   whose channels are swapped, or one that is off by one sample and has every
   channel on the wrong pin for the rest of the run. Per-channel figures make
   all three distinguishable, given two pins driven to different levels. */
static void doCapture(uint32_t ms, int nchannels) {
  if (!adc) { Serial1.println("adc_capture=not_started"); return; }
  if (nchannels < 1) nchannels = 1;
  if (nchannels > 8) nchannels = 8;
  adc->flush();
  adc->getOverflows();

  uint32_t count = 0;
  uint16_t lo[8], hi[8];
  uint64_t sum[8];
  uint32_t n_ch[8];
  for (int i = 0; i < 8; i++) { lo[i] = 0xFFFF; hi[i] = 0; sum[i] = 0; n_ch[i] = 0; }

  /* A multiple of the channel count, so a short read never shifts the phase
     of everything after it. */
  uint16_t buf[128];
  const size_t chunk = (sizeof(buf) / sizeof(buf[0])) / nchannels * nchannels;

  const uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    size_t n = adc->read(buf, chunk);
    for (size_t i = 0; i < n; i++) {
      const int c = (int)((count + i) % (uint32_t)nchannels);
      if (buf[i] < lo[c]) lo[c] = buf[i];
      if (buf[i] > hi[c]) hi[c] = buf[i];
      sum[c] += buf[i];
      n_ch[c]++;
    }
    count += n;
    yield();
  }
  const uint32_t elapsed = millis() - t0;

  Serial1.print("adc_samples="); Serial1.println(count);
  Serial1.print("adc_ms="); Serial1.println(elapsed);
  /* Samples per second divided by the channel count is the SCAN rate, which
     is what begin() was asked for. */
  const uint32_t sps = elapsed ? (uint32_t)((uint64_t)count * 1000 / elapsed) : 0;
  Serial1.print("adc_samples_per_sec="); Serial1.println(sps);
  Serial1.print("adc_scans_per_sec="); Serial1.println(sps / (uint32_t)nchannels);
  /* Whole scans only: a capture that ends mid-scan has one channel with an
     extra sample, and that is worth seeing rather than rounding away. */
  Serial1.print("adc_partial_scan="); Serial1.println(count % (uint32_t)nchannels);
  Serial1.print("adc_overflows="); Serial1.println(adc->getOverflows());

  for (int c = 0; c < nchannels; c++) {
    Serial1.print("adc_ch"); Serial1.print(c); Serial1.print("_n=");
    Serial1.println(n_ch[c]);
    Serial1.print("adc_ch"); Serial1.print(c); Serial1.print("_min=");
    Serial1.println(n_ch[c] ? lo[c] : 0);
    Serial1.print("adc_ch"); Serial1.print(c); Serial1.print("_max=");
    Serial1.println(n_ch[c] ? hi[c] : 0);
    Serial1.print("adc_ch"); Serial1.print(c); Serial1.print("_mean=");
    Serial1.println(n_ch[c] ? (uint32_t)(sum[c] / n_ch[c]) : 0);
  }
}

/* Drive an analog pin from its own GPIO output driver, so a capture has a
 * KNOWN value on it. The board has no signal generator attached and no free
 * wiring, and a floating pin reads whatever the sample-and-hold last held --
 * which is indistinguishable from a channel that is being read off the wrong
 * pin. PC0 and PC1 go nowhere on this board, so driving them is safe.
 *
 * The output driver stays enabled while the ADC samples the pad, which is
 * exactly what makes this work: the converter sees the driven level. It must
 * be re-applied AFTER begin(), because begin() puts every pin it samples into
 * analog mode. */
static void doDrive(int which, int level) {
  const pin_size_t pin = (which == 1) ? A1 : A0;
  if (level < 0) {
    pinMode(pin, INPUT);           /* back to floating */
  } else {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, level ? HIGH : LOW);
  }
  Serial1.print("adc_drive_pin="); Serial1.println(pin);
  Serial1.print("adc_drive_level="); Serial1.println(level);
}

static void handle(char *cmd) {
  if (!strncmp(cmd, "adcbegin", 8)) {
    /* adcbegin <rate> [spec], spec being the input codes above -- "0", "01",
       "tv", "0t" and so on. Defaults to A0 alone. */
    uint32_t rate = 8000;
    const char *spec = "0";
    char *sp = strchr(cmd, ' ');
    if (sp) {
      rate = (uint32_t)atol(sp + 1);
      char *sp2 = strchr(sp + 1, ' ');
      if (sp2) spec = sp2 + 1;
    }
    doBegin(rate, spec);

  } else if (!strncmp(cmd, "adccapture ", 11)) {
    uint32_t ms = (uint32_t)atol(cmd + 11);
    char *sp = strchr(cmd + 11, ' ');
    doCapture(ms, sp ? atoi(sp + 1) : 1);

  } else if (!strncmp(cmd, "adcdrive ", 9)) {
    /* adcdrive <0|1> <-1|0|1> -- which analog pin, and the level to hold it
       at (-1 releases it). */
    int which = atoi(cmd + 9);
    int level = -1;
    char *sp = strchr(cmd + 9, ' ');
    if (sp) level = atoi(sp + 1);
    doDrive(which, level);

  } else if (!strcmp(cmd, "adcend")) {
    if (adc) adc->end();
    Serial1.println("adc_end=1");

  } else if (!strncmp(cmd, "adcres ", 7)) {
    /* The converter is 12-bit and analogRead() defaults to Arduino's 10, so
       this is how a sketch gets the full range. Reported back through the
       core's own getter rather than the argument, so a rejected value is
       visible. */
    analogReadResolution(atoi(cmd + 7));
    Serial1.print("adc_res_bits="); Serial1.println(analogReadResolutionBits());
    Serial1.print("avref_at_res="); Serial1.println(analogRead(AVREF));

  } else if (!strncmp(cmd, "dacwrite ", 9)) {
    /* dacwrite <1|2> <value> [bits]
       Writes through analogWrite(), which is the whole point -- the DAC has no
       API of its own here -- then reads the same pad back with the ADC. The
       two DAC pins are ADC4 and ADC5, so the converter can check the
       converter with nothing wired to the board. */
    int which = atoi(cmd + 9);
    long value = 0;
    int bits = 12;
    char *sp = strchr(cmd + 9, ' ');
    if (sp) {
      value = atol(sp + 1);
      char *sp2 = strchr(sp + 1, ' ');
      if (sp2) bits = atoi(sp2 + 1);
    }
    const pin_size_t pin = (which == 2) ? DAC2 : DAC1;

    analogWriteResolution(bits);
    analogWrite(pin, (int)value);
    /* The buffer needs a moment to settle before the ADC samples it. */
    delayMicroseconds(200);

    analogReadResolution(12);
    Serial1.print("dac_pin="); Serial1.println(pin);
    Serial1.print("dac_channel="); Serial1.println(ch32h4_dac_channel(pin));
    Serial1.print("dac_started="); Serial1.println(ch32h4_dac_is_started(pin) ? 1 : 0);
    Serial1.print("dac_code="); Serial1.println(ch32h4_dac_read(pin));
    Serial1.print("dac_readback="); Serial1.println(analogRead(pin));
    analogReadResolution(10);

  } else if (!strncmp(cmd, "dacbuffer ", 10)) {
    /* dacbuffer <1|2> <0|1> -- the output buffer cannot reach either rail, so
       a full-range test has to turn it off. */
    int which = atoi(cmd + 10);
    int on = 0;
    char *sp = strchr(cmd + 10, ' ');
    if (sp) on = atoi(sp + 1);
    const pin_size_t pin = (which == 2) ? DAC2 : DAC1;
    Serial1.print("dac_buffer_set=");
    Serial1.println(ch32h4_dac_output_buffer(pin, on != 0) ? 1 : 0);
    Serial1.print("dac_buffer="); Serial1.println(on);

  } else if (!strncmp(cmd, "dacstop ", 8)) {
    const pin_size_t pin = (atoi(cmd + 8) == 2) ? DAC2 : DAC1;
    analogWriteStop(pin);
    Serial1.print("dac_started="); Serial1.println(ch32h4_dac_is_started(pin) ? 1 : 0);

  } else if (!strcmp(cmd, "dacpins")) {
    Serial1.print("pin_dac1="); Serial1.println(DAC1);
    Serial1.print("pin_dac2="); Serial1.println(DAC2);
    Serial1.print("dac1_has_dac="); Serial1.println(ch32h4_pin_has_dac(DAC1) ? 1 : 0);
    Serial1.print("dac2_has_dac="); Serial1.println(ch32h4_pin_has_dac(DAC2) ? 1 : 0);
    Serial1.print("a0_has_dac="); Serial1.println(ch32h4_pin_has_dac(A0) ? 1 : 0);
    /* Both DAC pins are ADC inputs too -- that is what makes the readback
       possible, and it was missing from the variant until the DAC went in. */
    Serial1.print("dac1_adc_ch="); Serial1.println(ch32h4_adc_channel(DAC1));
    Serial1.print("dac2_adc_ch="); Serial1.println(ch32h4_adc_channel(DAC2));
    Serial1.print("num_analog="); Serial1.println(NUM_ANALOG_INPUTS);
    Serial1.print("a10_ch="); Serial1.println(ch32h4_adc_channel(A10));
    Serial1.print("a15_ch="); Serial1.println(ch32h4_adc_channel(A15));

  } else if (!strcmp(cmd, "adcvref")) {
    /* analogRead() refuses while a paced capture owns the ADC, and reports 0.
       Surfacing the flag keeps that distinguishable from a dead channel. */
    Serial1.print("adc_capturing="); Serial1.println(ch32h4_adc_is_capturing());
    /* The one known input this board has without wiring anything. If VDDA is
       3.3 V then a nominal 1.20 V reference reads 4095*1.2/3.3 = 1489. */
    Serial1.print("vdda_volts_x1000=");
    Serial1.println((uint32_t)(ch32h4_vdda_volts() * 1000.0f));
    Serial1.print("a0_single=");
    Serial1.println(analogRead(A0));
    /* The internal channels through the ordinary analogRead() path, by their
       pseudo-pin numbers. */
    Serial1.print("avref_raw="); Serial1.println(analogRead(AVREF));
    Serial1.print("atemp_raw="); Serial1.println(analogRead(ATEMP));
    Serial1.print("temp_c_x100=");
    Serial1.println((int32_t)(analogReadTemp() * 100.0f));
    Serial1.print("pin_atemp="); Serial1.println(ATEMP);
    Serial1.print("pin_avref="); Serial1.println(AVREF);

  } else if (!strcmp(cmd, "adcpins")) {
    Serial1.print("pin_a0="); Serial1.println(A0);
    Serial1.print("pin_a1="); Serial1.println(A1);
    Serial1.print("num_analog="); Serial1.println(NUM_ANALOG_INPUTS);
  }
  Serial1.print("> ");
}

void setup() {
  Serial1.begin(115200);
  Serial1.println("adctest starting");
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
