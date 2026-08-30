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
