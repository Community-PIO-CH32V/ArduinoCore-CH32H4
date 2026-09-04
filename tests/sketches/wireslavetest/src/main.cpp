/* I2C master and I2C slave on the same chip, on one bus.

   I2C is multi-drop, so this needs no bus of its own: Wire (I2C1, PB6/PB7)
   is the master and Wire1 (I2C4, PD12/PD13) the slave, joined to the same two
   wires. The SSD1306 already on those pins stays where it is and supplies the
   pull-ups for all three devices -- this part has no internal ones in
   open-drain mode, so without the display's resistors nothing here works.

   Bench wiring, two jumpers:
       PB6 -> PD12   SCL
       PB7 -> PD13   SDA

   The slave answers at 0x42, which is not 0x3C, so it and the display can
   share the bus without either one answering for the other.
*/
#include <Arduino.h>
#include <Wire.h>

#define SLAVE_ADDR 0x42

static const size_t MAXN = 128;

/* Written by the slave's callbacks, which run in interrupt context. */
static volatile uint32_t s_recvCount = 0;
static volatile uint32_t s_reqCount = 0;
static volatile int s_lastLen = 0;
static uint8_t s_lastBuf[MAXN];

/* What the slave answers a read with. Refilled by the sketch, consumed by the
   request handler. */
static uint8_t s_reply[MAXN];
static volatile size_t s_replyLen = 0;

static void onReceiveHandler(int len) {
  if (len > (int)MAXN) {
    len = (int)MAXN;
  }
  for (int i = 0; i < len; i++) {
    s_lastBuf[i] = (uint8_t)Wire1.read();
  }
  s_lastLen = len;
  s_recvCount++;
}

static void onRequestHandler(void) {
  /* Runs with the master already clocking. Fill the buffer and return --
     anything that waits here stretches the bus clock. */
  Wire1.write(s_reply, s_replyLen);
  s_reqCount++;
}

void setup() {
  Serial1.begin(115200);

  Wire1.onReceive(onReceiveHandler);
  Wire1.onRequest(onRequestHandler);
  Wire1.begin(SLAVE_ADDR);

  Wire.begin();
  Wire.setClock(100000);

  Serial1.print("wis_master=");
  Serial1.println(Wire.peripheral());
  Serial1.print("wis_slave=");
  Serial1.println(Wire1.peripheral());
  Serial1.print("wis_addr=0x");
  Serial1.println(Wire1.slaveAddress(), HEX);
  Serial1.println("wireslavetest ready");
  Serial1.print("> ");
}

void loop() {
  static String line;
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        if (line == "wisinfo") {
          Serial1.print("wis_master=");
          Serial1.println(Wire.peripheral());
          Serial1.print("wis_slave=");
          Serial1.println(Wire1.peripheral());
          Serial1.print("wis_addr=0x");
          Serial1.println(Wire1.slaveAddress(), HEX);

        } else if (line == "wisscan") {
          /* The slave must show up in a bus scan done by the master beside
             it. This is the whole of slave mode in one line: if the address
             is not acknowledged, nothing else can work. */
          int found = 0;
          for (uint8_t a = 8; a < 120; a++) {
            Wire.beginTransmission(a);
            if (Wire.endTransmission() == 0) {
              Serial1.print("wis_found=0x");
              Serial1.println(a, HEX);
              found++;
            }
          }
          Serial1.print("wis_scan_count=");
          Serial1.println(found);

        } else if (line.startsWith("wiswrite ")) {
          /* Master writes n bytes; the slave's receive handler must see
             exactly those n bytes, and see them after the STOP rather than
             one transfer late. */
          const size_t n = (size_t)line.substring(9).toInt();
          if (n == 0 || n > MAXN) {
            Serial1.println("wis_write=bad_length");
          } else {
            static uint8_t tx[MAXN];
            for (size_t i = 0; i < n; i++) {
              tx[i] = (uint8_t)(0x30 + i * 3);
            }
            const uint32_t before = s_recvCount;
            Wire.beginTransmission(SLAVE_ADDR);
            Wire.write(tx, n);
            const uint8_t rc = Wire.endTransmission();

            const uint32_t deadline = millis() + 100;
            while (s_recvCount == before && millis() < deadline) {
            }

            Serial1.print("wis_write_rc=");
            Serial1.println(rc);
            Serial1.print("wis_write_cb=");
            Serial1.println((int)(s_recvCount - before));
            Serial1.print("wis_write_len=");
            Serial1.println(s_lastLen);
            Serial1.print("wis_write_match=");
            Serial1.println((s_lastLen == (int)n &&
                             memcmp(s_lastBuf, tx, n) == 0) ? 1 : 0);
          }

        } else if (line.startsWith("wisread ")) {
          /* Master reads n bytes the slave queued. The last byte is the one
             worth watching: the master ends a read by NOT acknowledging it,
             which raises the acknowledge-failure flag on the slave, and a
             slave that treats that as an error rather than as the end of a
             read never answers again. */
          const size_t n = (size_t)line.substring(8).toInt();
          if (n == 0 || n > MAXN) {
            Serial1.println("wis_read=bad_length");
          } else {
            for (size_t i = 0; i < n; i++) {
              s_reply[i] = (uint8_t)(0x90 + i * 7);
            }
            s_replyLen = n;

            static uint8_t rx[MAXN];
            memset(rx, 0, n);
            const size_t got = Wire.requestFrom((uint8_t)SLAVE_ADDR, n);
            size_t i = 0;
            while (Wire.available() && i < n) {
              rx[i++] = (uint8_t)Wire.read();
            }

            Serial1.print("wis_read_got=");
            Serial1.println((int)got);
            Serial1.print("wis_read_match=");
            Serial1.println((got == n && memcmp(rx, s_reply, n) == 0) ? 1 : 0);
            Serial1.print("wis_read_r0=0x"); Serial1.println(rx[0], HEX);
            Serial1.print("wis_read_rn=0x"); Serial1.println(rx[n - 1], HEX);
          }

        } else if (line == "wisrepeat") {
          /* Three transfers back to back. A slave that leaves a flag set
             answers the first and then goes quiet, which a single-shot test
             reports as success. */
          int ok = 0;
          for (int round = 0; round < 3; round++) {
            uint8_t tx[4] = {(uint8_t)round, 0xAA, 0x55, (uint8_t)(round + 1)};
            const uint32_t before = s_recvCount;
            Wire.beginTransmission(SLAVE_ADDR);
            Wire.write(tx, 4);
            if (Wire.endTransmission() != 0) {
              continue;
            }
            const uint32_t deadline = millis() + 100;
            while (s_recvCount == before && millis() < deadline) {
            }
            if (s_lastLen == 4 && memcmp(s_lastBuf, tx, 4) == 0) {
              ok++;
            }

            s_reply[0] = (uint8_t)(0xB0 + round);
            s_replyLen = 1;
            uint8_t back = 0;
            if (Wire.requestFrom((uint8_t)SLAVE_ADDR, (size_t)1) == 1) {
              back = (uint8_t)Wire.read();
            }
            if (back == (uint8_t)(0xB0 + round)) {
              ok++;
            }
          }
          Serial1.print("wis_repeat_ok=");
          Serial1.println(ok);   /* 6 when every round worked both ways */

        } else if (line == "wisempty") {
          /* A read the handler supplies nothing for. It must not repeat the
             last reply: stale bytes from the previous transfer are the
             failure that looks like a working device until the timing
             shifts. */
          s_reply[0] = 0x5A;
          s_replyLen = 1;
          uint8_t first = 0;
          if (Wire.requestFrom((uint8_t)SLAVE_ADDR, (size_t)1) == 1) {
            first = (uint8_t)Wire.read();
          }
          s_replyLen = 0;
          uint8_t second = 0;
          if (Wire.requestFrom((uint8_t)SLAVE_ADDR, (size_t)1) == 1) {
            second = (uint8_t)Wire.read();
          }
          Serial1.print("wis_empty_first=0x"); Serial1.println(first, HEX);
          Serial1.print("wis_empty_second=0x"); Serial1.println(second, HEX);
          Serial1.print("wis_empty_stale=");
          Serial1.println(second == first ? 1 : 0);

        } else if (line == "wisdisplay") {
          /* The display must still work with a second peripheral sharing its
             bus. A slave that holds SDA low between transfers takes the whole
             bus down with it, and nothing else here would notice. */
          Wire.beginTransmission(0x3C);
          Wire.write((uint8_t)0x00);
          Wire.write((uint8_t)0xAE);   /* display off: a no-op it must ACK */
          Serial1.print("wis_display_rc=");
          Serial1.println(Wire.endTransmission());

        } else {
          Serial1.println("?");
        }
      }
      line = "";
      Serial1.print("> ");
    } else {
      line += c;
    }
  }
}
