/* The extra USARTs, and whether bytes actually move.
 *
 * NEEDS ONE JUMPER: PA2 to PA3, which is USART2's default TX to its own RX.
 * Without it every round-trip reports zero bytes and the tests skip rather
 * than fail -- an unwired bench is a missing precondition, not a broken
 * driver.
 *
 * Loopback on one peripheral is the strongest cheap test there is here: it
 * runs the whole path -- pin mux, baud divisor, TX shift register, the wire,
 * RX, the RXNE interrupt and the ring buffer -- and a wrong alternate function
 * anywhere in it produces silence rather than a wrong answer.
 */
#include <Arduino.h>

static char line[128];
static int len = 0;

static void roundTrip(unsigned long baud, uint16_t cfg) {
  Serial2.end();
  Serial2.begin(baud, cfg);
  delay(5);
  while (Serial2.available()) { Serial2.read(); }

  static const char msg[] = "The quick brown fox 0123456789";
  const size_t n = sizeof(msg) - 1;
  Serial2.write((const uint8_t *)msg, n);
  Serial2.flush();

  uint32_t t0 = millis();
  size_t got = 0;
  char buf[64];
  while (got < n && millis() - t0 < 300) {
    if (Serial2.available()) { buf[got++] = (char)Serial2.read(); }
  }
  Serial1.print("uart_got="); Serial1.println((uint32_t)got);
  Serial1.print("uart_match=");
  Serial1.println((got == n && memcmp(buf, msg, n) == 0) ? 1 : 0);
}

static void handle(const char *cmd) {
  if (!strcmp(cmd, "uartinfo")) {
    Serial1.print("uart_id="); Serial1.println(Serial2.id());
    /* A pin the silicon cannot use for this signal must be refused, not
       accepted and then silently ignored. */
    Serial1.print("uart_good_pin="); Serial1.println(Serial2.setTX(PA2) ? 1 : 0);
    Serial1.print("uart_bad_pin_refused=");
    Serial1.println(Serial2.setTX(PB0) ? 0 : 1);

  } else if (!strncmp(cmd, "uartloop ", 9)) {
    const unsigned long baud = (unsigned long)atol(cmd + 9);
    roundTrip(baud, SERIAL_8N1);

  } else if (!strcmp(cmd, "uartparity")) {
    roundTrip(115200, SERIAL_8E1);

  } else if (!strcmp(cmd, "uartstop2")) {
    roundTrip(115200, SERIAL_8N2);

  } else if (!strcmp(cmd, "uartoverflow")) {
    /* More than the buffer holds. It must bound and drop, not wrap and hand
       back bytes from the wrong end of the stream. */
    Serial2.end(); Serial2.begin(115200);
    delay(5); while (Serial2.available()) { Serial2.read(); }
    for (int i = 0; i < 400; i++) { Serial2.write((uint8_t)('A' + (i % 26))); }
    Serial2.flush();
    delay(80);
    const int n = Serial2.available();
    Serial1.print("uart_overflow="); Serial1.println(n);
    Serial1.print("uart_overflow_bounded="); Serial1.println(n <= 255 ? 1 : 0);
  }
  Serial1.print("> ");
}

void setup() {
  Serial1.begin(115200);
  Serial1.println("uarttest ready");
  Serial1.print("> ");
}

void loop() {
  while (Serial1.available()) {
    const char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (len) { line[len] = 0; handle(line); len = 0; }
    } else if (len < (int)sizeof(line) - 1) {
      line[len++] = c;
    }
  }
}
