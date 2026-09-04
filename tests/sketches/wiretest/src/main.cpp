#include <Arduino.h>
#include <Wire.h>

void setup() {
  Serial1.begin(115200);
  Wire.begin();
  Serial1.print("i2c_peripheral=");
  Serial1.println(Wire.peripheral());
  Serial1.println("wiretest ready");
  Serial1.print("> ");
}

void loop() {
  static String line;
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        if (line == "i2cinfo") {
          Serial1.print("i2c_peripheral=");
          Serial1.println(Wire.peripheral());
        } else if (line.startsWith("i2cread ")) {
          /* Short reads are where an I2C master gets it wrong. For a one-byte
             read the ACK bit has to be cleared BEFORE the address phase is
             cleared: the peripheral decides what to drive on the ninth clock
             of the first byte as soon as ADDR is released, so doing it
             afterwards is too late and the device sends a second byte nobody
             asked for. That desynchronises every transfer after it, so the
             symptom appears somewhere else entirely. */
          const size_t n = (size_t)line.substring(8).toInt();
          const size_t got = Wire.requestFrom((uint8_t)0x3C, n);
          Serial1.print("i2c_read_got="); Serial1.println((uint32_t)got);
          Serial1.print("i2c_read_avail="); Serial1.println((uint32_t)Wire.available());
          while (Wire.available()) { Wire.read(); }
          /* The bus must still work afterwards. A left-over ACK shows up as
             the NEXT write failing, not as the read failing. */
          Wire.beginTransmission((uint8_t)0x3C);
          Wire.write((uint8_t)0x00);
          Serial1.print("i2c_after_read_rc="); Serial1.println(Wire.endTransmission());

        } else if (line.startsWith("i2cbench ")) {
          /* Throughput against the theoretical bus time: 9 bits per byte, 8
             data plus the acknowledge slot, and one extra byte for the
             address. I2C is slow enough that a polled driver is already at
             the wire's limit -- which is why this one has no DMA path, unlike
             SPI, where polling cost five sixths of the bus. */
          const uint32_t hz = (uint32_t)line.substring(9).toInt();
          static uint8_t payload[128];
          Wire.setClock(hz);
          const uint32_t t0 = micros();
          Wire.beginTransmission((uint8_t)0x3C);
          Wire.write(payload, sizeof(payload));
          const uint8_t rc = Wire.endTransmission();
          const uint32_t us = micros() - t0;
          const uint32_t ideal =
              (uint32_t)((uint64_t)(sizeof(payload) + 1) * 9 * 1000000 / hz);
          Serial1.print("i2c_bench_rc="); Serial1.println(rc);
          Serial1.print("i2c_bench_pct=");
          Serial1.println(us ? ideal * 100 / us : 0);

        } else if (line == "i2cabsent") {
          /* A device that is not there must report 2 (address NACK) and leave
             the bus usable, not hang and not report a bus fault. */
          Wire.beginTransmission((uint8_t)0x7A);
          Serial1.print("i2c_absent_rc="); Serial1.println(Wire.endTransmission());
          Serial1.print("i2c_absent_read=");
          Serial1.println((uint32_t)Wire.requestFrom((uint8_t)0x7A, (size_t)1));

        } else if (line == "i2cscan") {
          /* The board's SSD1306 answers at 0x3C or 0x3D -- both are shipped
             and the module does not say which, so probe rather than assume. */
          int found = 0;
          for (uint8_t a = 8; a < 120; a++) {
            Wire.beginTransmission(a);
            if (Wire.endTransmission() == 0) {
              found++;
              Serial1.print("i2c_found=0x");
              Serial1.println(a, HEX);
            }
          }
          Serial1.print("i2c_devices=");
          Serial1.println(found);
        } else if (line == "i2crecover") {
          Serial1.print("i2c_recover=");
          Serial1.println(Wire.recover() ? "ok" : "stuck");
        } else if (line == "i2cbadpins") {
          /* PB6 is I2C1 SCL but PB11 is I2C2 SDA, so the pair is not one the
             silicon offers and must be refused rather than half-configured. */
          TwoWire bad(PB6, PB11);
          bad.begin();
          Serial1.print("bad_peripheral=");
          Serial1.println(bad.peripheral());
        }
        Serial1.print("> ");
        line = "";
      }
    } else {
      line += c;
    }
  }
  yield();
}
