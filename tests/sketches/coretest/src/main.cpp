/* The on-target half of the test suite.
 *
 * Every hardware test drives this sketch over the console: it reads a command
 * line and prints one or more `key=value` lines, then a "> " prompt. Keeping
 * the assertions on the HOST means a test can measure the board against the
 * host's clock rather than against a number the board reported about itself,
 * which is the only way to catch a factor-of-four clock error.
 *
 * A missing precondition -- an absent jumper, an unplugged display -- reports
 * itself as a missing precondition, so the test skips rather than fails.
 * Three debugging sessions in the libhal port began by trusting a red test
 * that was reporting a loose wire.
 */
#include <Arduino.h>

/* The test protocol deliberately runs on Serial1 -- USART1, into the
 * WCH-Link's VCP -- and NOT on `Serial`.
 *
 * `Serial` is USB CDC by default, and a CDC port only exists once a host has
 * enumerated it. Running the suite over it would make every test depend on
 * enumeration timing and on the host having opened the port, and a failure
 * there would look like a firmware failure. The UART is up before the V5F even
 * starts. USB is exercised by its own commands below and by a host-side test
 * that opens the CDC port directly. */

static String line;

static volatile int irqCount = 0;
static void onEdge() { irqCount++; }

/* The XIP-vs-ITCM benchmark. The same loop compiled twice, so the only
   variable is where the instructions live. */
static uint32_t workXip(volatile uint32_t *acc, int n) {
  uint32_t h = 0;
  for (int i = 0; i < n; i++) { h = h * 31u + (uint32_t)i; *acc = h; }
  return h;
}
__itcm_func static uint32_t workItcm(volatile uint32_t *acc, int n) {
  uint32_t h = 0;
  for (int i = 0; i < n; i++) { h = h * 31u + (uint32_t)i; *acc = h; }
  return h;
}

static void handleCommand(const String &cmd) {
  if (cmd == "ping") {
    Serial1.println("pong");

  } else if (cmd.startsWith("echo ")) {
    Serial1.print("echo:");
    Serial1.println(cmd.substring(5));

  } else if (cmd == "millis") {
    Serial1.print("millis=");
    Serial1.println(millis());

  } else if (cmd == "delaytest") {
    uint32_t t0 = micros();
    delay(1000);
    uint32_t t1 = micros();
    Serial1.print("delay1000_us=");
    Serial1.println(t1 - t0);

  } else if (cmd == "microstest") {
    bool ok = true;
    uint32_t prev = micros();
    for (int i = 0; i < 20000; i++) {
      uint32_t n = micros();
      if (n < prev) { ok = false; break; }
      prev = n;
    }
    Serial1.print("micros_monotonic=");
    Serial1.println(ok ? "ok" : "FAIL");

  } else if (cmd == "delayustest") {
    uint32_t t0 = micros();
    for (int i = 0; i < 1000; i++) { delayMicroseconds(100); }
    uint32_t t1 = micros();
    Serial1.print("delayus_total=");
    Serial1.println(t1 - t0);

  } else if (cmd == "serialinfo") {
    Serial1.print("hclk=");
    Serial1.println(ch32h4_hclk());

  } else if (cmd == "printtest") {
    Serial1.print("int="); Serial1.println(42);
    Serial1.print("float="); Serial1.println(3.14, 2);
    Serial1.print("str="); Serial1.println("abc");
    Serial1.print("hex="); Serial1.println(255, HEX);

  } else if (cmd == "rcctest") {
    ch32h4_clock_enable(CH32_BUS_HB2, RCC_HB2Periph_GPIOC);
    bool ok = (RCC->HB2PCENR & RCC_HB2Periph_GPIOC) != 0;
    Serial1.print("rcc_readback=");
    Serial1.println(ok ? "ok" : "FAIL");

    uint32_t before = ch32h4_reset_refused_count;
    ch32h4_block_reset(CH32_BUS_HB2, RCC_HB2Periph_GPIOB);
    ch32h4_block_reset(CH32_BUS_HB1, RCC_HB1Periph_PWR);
    ch32h4_block_reset(CH32_BUS_HB,  RCC_HBPeriph_DMA1);
    ch32h4_block_reset(CH32_BUS_HB,  RCC_HBPeriph_ETH);
    Serial1.print("reset_refused=");
    Serial1.println(ch32h4_reset_refused_count - before);

  } else if (cmd == "gpiotest") {
    pinMode(PIN_JUMPER_A, OUTPUT);
    pinMode(PIN_JUMPER_B, INPUT);
    digitalWrite(PIN_JUMPER_A, HIGH);
    delayMicroseconds(50);
    bool hi = digitalRead(PIN_JUMPER_B);
    digitalWrite(PIN_JUMPER_A, LOW);
    delayMicroseconds(50);
    bool lo = digitalRead(PIN_JUMPER_B) == 0;
    bool jumper = hi && lo;
    Serial1.print("jumper_pc3_pc4=");
    Serial1.println(jumper ? 1 : 0);
    Serial1.print("gpio_loopback=");
    Serial1.println(jumper ? "ok" : "absent");

    /* Release the driving pin first. With the PC3-PC4 jumper fitted, an
       output still holding the line low would mask the pull-up entirely --
       and the reading would be correct hardware behaviour reported as a
       broken pull-up. */
    pinMode(PIN_JUMPER_A, INPUT);
    pinMode(PIN_JUMPER_B, INPUT_PULLUP);
    delayMicroseconds(200);
    Serial1.print("gpio_pullup=");
    Serial1.println(digitalRead(PIN_JUMPER_B));

  } else if (cmd == "irqtest") {
    irqCount = 0;
    pinMode(PIN_JUMPER_A, OUTPUT);
    digitalWrite(PIN_JUMPER_A, LOW);
    pinMode(PIN_JUMPER_B, INPUT);
    attachInterrupt(digitalPinToInterrupt(PIN_JUMPER_B), onEdge, RISING);
    delay(1);
    digitalWrite(PIN_JUMPER_A, HIGH);
    delay(1);
    detachInterrupt(digitalPinToInterrupt(PIN_JUMPER_B));
    Serial1.print("irq_count=");
    Serial1.println(irqCount);

  } else if (cmd == "irqconflict") {
    /* PA0 and PB0 both map to EXTI line 0. The second request must be refused,
       not silently granted by stealing the line from the first. */
    detachInterrupt(digitalPinToInterrupt(PA0));
    attachInterrupt(digitalPinToInterrupt(PA0), onEdge, RISING);
    int first = ch32h4_exti_owner(0);
    attachInterrupt(digitalPinToInterrupt(PB0), onEdge, RISING);
    int after = ch32h4_exti_owner(0);
    Serial1.print("attach_second=");
    Serial1.println(first == after ? "refused" : "STOLEN");
    Serial1.print("first_still_owns=");
    Serial1.println(after == PA0 ? 1 : 0);
    detachInterrupt(digitalPinToInterrupt(PA0));

  } else if (cmd == "vref") {
    Serial1.print("vdda=");
    Serial1.println(ch32h4_vdda_volts(), 3);

  } else if (cmd == "adcinfo") {
    Serial1.print("adcclk=");
    Serial1.println(ch32h4_hclk() / 8);

  } else if (cmd == "adcread") {
    Serial1.print("a0_raw=");
    Serial1.println(analogRead(A0));

  } else if (cmd == "pwmtest") {
    analogWriteResolution(8);
    analogWrite(PA0, 128);
    delay(5);
    ch32h4_pwm_af_t af;
    ch32h4_pwm_find_active(PA0, &af);
    TIM_TypeDef *dev = ch32h4_timer_dev(af.timer);
    uint32_t top = dev->ATRLR + 1;
    Serial1.print("pwm_duty_pct=");
    Serial1.println((dev->CH1CVR * 100) / top);
    Serial1.print("pwm_timer=");
    Serial1.println(af.timer);
    Serial1.print("pwm_owner=");
    Serial1.println(ch32h4_timer_owner_name(ch32h4_timer_owner(af.timer)));
    analogWriteStop(PA0);
    Serial1.print("pwm_released=");
    Serial1.println(ch32h4_timer_owner(af.timer) == CH32H4_TIMER_FREE ? 1 : 0);

  } else if (cmd == "pwmmap") {
    /* How many pins this package can actually do PWM on, and across how many
       distinct timers -- the point of having a real map rather than four
       hardcoded pins. */
    int pins = 0;
    uint16_t timers = 0;
    for (int i = 0; i < PINS_COUNT; i++) {
      if (ch32h4_pin_has_pwm(i)) { pins++; }
    }
    for (size_t i = 0; i < g_pwm_af_map_len; i++) {
      timers |= (uint16_t)(1u << g_pwm_af_map[i].timer);
    }
    int ntimers = 0;
    for (int i = 1; i <= CH32H4_TIMER_COUNT; i++) {
      if (timers & (1u << i)) { ntimers++; }
    }
    Serial1.print("pwm_pins=");
    Serial1.println(pins);
    Serial1.print("pwm_timers=");
    Serial1.println(ntimers);
    Serial1.print("pwm_entries=");
    Serial1.println((uint32_t)g_pwm_af_map_len);

  } else if (cmd == "pwmshare") {
    /* Two pins on the same timer must share it, not fight over it. PA0 and
       PA1 are TIM2 CH1 and CH2. */
    analogWriteStop(PA0); analogWriteStop(PA1);
    analogWrite(PA0, 64);
    ch32h4_pwm_af_t a0; ch32h4_pwm_find_active(PA0, &a0);
    analogWrite(PA1, 192);
    ch32h4_pwm_af_t a1; ch32h4_pwm_find_active(PA1, &a1);
    Serial1.print("share_same_timer=");
    Serial1.println(a0.timer == a1.timer ? 1 : 0);
    /* Releasing one must NOT release the timer while the other still runs. */
    analogWriteStop(PA0);
    Serial1.print("still_owned=");
    Serial1.println(ch32h4_timer_owner(a1.timer) == CH32H4_TIMER_PWM ? 1 : 0);
    analogWriteStop(PA1);
    Serial1.print("now_free=");
    Serial1.println(ch32h4_timer_owner(a1.timer) == CH32H4_TIMER_FREE ? 1 : 0);

  } else if (cmd == "timerclaim") {
    /* A timer held by something else must be refused, and analogWrite must
       then fall through to another timer the pin can use rather than
       producing a broken output on the one it wanted. */
    ch32h4_timer_release(2, CH32H4_TIMER_SERVO);
    analogWriteStop(PA0);
    bool got = ch32h4_timer_claim(2, CH32H4_TIMER_SERVO);
    Serial1.print("servo_claimed_tim2=");
    Serial1.println(got ? 1 : 0);
    Serial1.print("pwm_refused=");
    Serial1.println(ch32h4_timer_claim(2, CH32H4_TIMER_PWM) ? 0 : 1);
    Serial1.print("holder=");
    Serial1.println(ch32h4_timer_owner_name(ch32h4_timer_owner(2)));
    /* PA0 is also TIM5 CH1 and TIM9 CH1, so it should still work. */
    analogWrite(PA0, 100);
    ch32h4_pwm_af_t af;
    bool active = ch32h4_pwm_find_active(PA0, &af);
    Serial1.print("fellback_to_timer=");
    Serial1.println(active ? af.timer : 0);
    analogWriteStop(PA0);
    ch32h4_timer_release(2, CH32H4_TIMER_SERVO);

  } else if (cmd == "usbstat") {
    Serial1.print("usb_active=");
    Serial1.println(ch32h4_usb_active() ? 1 : 0);
    Serial1.print("usb_connected=");
    Serial1.println(TinyUSBDevice.mounted() ? 1 : 0);

  } else if (cmd == "usbwrite") {
    /* Push a known string out of the CDC port for the host to collect. */
    Serial.println("hello-from-usb");
    Serial.flush();
    Serial1.println("usb_wrote=1");

  } else if (cmd == "heapinfo") {
    Serial1.print("heap_free=");
    Serial1.println((uint32_t)ch32h4_heap_free());

  } else if (cmd == "bigalloc") {
    /* Large enough that it cannot come from DTCM alone, so it proves the sbrk
       hand-off into the shared region. */
    void *p = malloc(300 * 1024);
    Serial1.print("big_alloc=");
    Serial1.println(p ? "ok" : "FAIL");
    free(p);

  } else if (cmd == "throwtest") {
#ifdef CH32H4_EXCEPTIONS
    try {
      throw 42;
    } catch (int v) {
      Serial1.print("caught=");
      Serial1.println(v);
    }
#else
    Serial1.println("caught=disabled");
#endif

  } else if (cmd == "bench") {
    volatile uint32_t acc = 0;
    uint32_t t0 = micros(); workXip(&acc, 200000);  uint32_t t1 = micros();
    uint32_t t2 = micros(); workItcm(&acc, 200000); uint32_t t3 = micros();
    Serial1.print("xip_us=");
    Serial1.print(t1 - t0);
    Serial1.print(" itcm_us=");
    Serial1.println(t3 - t2);

  } else if (cmd == "blinktest") {
    /* No LED is wired on this board, so count the transitions the driver
       actually produced by reading the pad back. */
    pinMode(PIN_JUMPER_A, OUTPUT);
    int transitions = 0;
    int last = digitalRead(PIN_JUMPER_A);
    for (int i = 0; i < 10; i++) {
      digitalWrite(PIN_JUMPER_A, (i & 1) ? HIGH : LOW);
      delay(5);
      int now = digitalRead(PIN_JUMPER_A);
      if (now != last) { transitions++; }
      last = now;
    }
    Serial1.print("blink_transitions=");
    Serial1.println(transitions);

  } else if (cmd == "crash") {
    Serial1.println("crashing");
    Serial1.flush();
    /* An all-zero word is an illegal instruction on RISC-V, so this traps with
       mcause=2. A store to an unmapped address does NOT trap on this bus --
       it is simply discarded -- which makes it useless as a fault trigger. */
    /* An all-zero word is an illegal instruction on RISC-V, so this traps
       with mcause=2. A store to an unmapped address does NOT trap on this
       bus -- it is simply discarded -- which makes it useless as a trigger. */
    __asm volatile(".word 0x00000000");

  } else if (cmd.length()) {
    Serial1.print("unknown=");
    Serial1.println(cmd);
  }

  Serial1.print("> ");
}

void setup() {
  Serial1.begin(115200);
  Serial.begin(115200);      /* the CDC port; begin() is a no-op for line coding */
  Serial1.println("coretest ready");
  Serial1.print("> ");
}

void loop() {
  /* Echo anything arriving on the CDC port straight back, so a host-side test
     can prove the USB path end to end rather than just that it enumerated. */
  while (Serial.available()) {
    int c = Serial.read();
    if (c >= 0) {
      Serial.write((uint8_t)c);
    }
  }

  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        handleCommand(line);
        line = "";
      }
    } else {
      line += c;
    }
  }
}
