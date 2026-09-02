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

static void doBegin(uint32_t rate, int nchannels) {
  delete adc;
  adc = nullptr;

  if (nchannels == 1) {
    adc = new ADCInput(A0);
  } else {
    adc = new ADCInput(A0, A1);
    nchannels = 2;
  }
  if (!adc) { Serial1.println("adc_begin=alloc_failed"); return; }
  adc->setBuffer(4096);

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
   millis() rather than against the timer that is producing it. */
static void doCapture(uint32_t ms, int nchannels) {
  if (!adc) { Serial1.println("adc_capture=not_started"); return; }
  adc->flush();
  adc->getOverflows();

  uint32_t count = 0;
  uint16_t lo = 0xFFFF, hi = 0;
  uint64_t sum = 0;
  uint16_t buf[128];

  const uint32_t t0 = millis();
  while (millis() - t0 < ms) {
    size_t n = adc->read(buf, sizeof(buf) / sizeof(buf[0]));
    for (size_t i = 0; i < n; i++) {
      if (buf[i] < lo) lo = buf[i];
      if (buf[i] > hi) hi = buf[i];
      sum += buf[i];
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
  Serial1.print("adc_scans_per_sec="); Serial1.println(sps / nchannels);
  Serial1.print("adc_min="); Serial1.println(count ? lo : 0);
  Serial1.print("adc_max="); Serial1.println(count ? hi : 0);
  Serial1.print("adc_mean="); Serial1.println(count ? (uint32_t)(sum / count) : 0);
  Serial1.print("adc_overflows="); Serial1.println(adc->getOverflows());
}

static void handle(char *cmd) {
  if (!strncmp(cmd, "adcbegin", 8)) {
    uint32_t rate = 8000;
    int nch = 1;
    char *sp = strchr(cmd, ' ');
    if (sp) {
      rate = (uint32_t)atol(sp + 1);
      char *sp2 = strchr(sp + 1, ' ');
      if (sp2) nch = atoi(sp2 + 1);
    }
    doBegin(rate, nch);

  } else if (!strncmp(cmd, "adccapture ", 11)) {
    uint32_t ms = (uint32_t)atol(cmd + 11);
    char *sp = strchr(cmd + 11, ' ');
    doCapture(ms, sp ? atoi(sp + 1) : 1);

  } else if (!strcmp(cmd, "adcend")) {
    if (adc) adc->end();
    Serial1.println("adc_end=1");

  } else if (!strcmp(cmd, "adcvref")) {
    /* The one known input this board has without wiring anything. If VDDA is
       3.3 V then a nominal 1.20 V reference reads 4095*1.2/3.3 = 1489. */
    Serial1.print("vdda_volts_x1000=");
    Serial1.println((uint32_t)(ch32h4_vdda_volts() * 1000.0f));
    Serial1.print("a0_single=");
    Serial1.println(analogRead(A0));

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
