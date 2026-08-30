#include <Arduino.h>
#include <SPI.h>

void setup() {
  Serial1.begin(115200);
  SPI.begin();
  Serial1.print("spi_peripheral=");
  Serial1.println(SPI.peripheral());
  Serial1.println("spitest ready");
  Serial1.print("> ");
}

void loop() {
  static String line;
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        if (line == "spiinfo") {
          Serial1.print("spi_peripheral=");
          Serial1.println(SPI.peripheral());
        } else if (line == "spiloop") {
          /* PA6-PA7 jumper: MISO tied to MOSI, so a transfer returns what it
             sent. The board carries that jumper so a test can prove a wire
             rather than a register. */
          SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
          uint8_t got = SPI.transfer(0xA5);
          uint8_t got2 = SPI.transfer(0x5A);
          SPI.endTransaction();
          Serial1.print("spi_loop_a5=0x"); Serial1.println(got, HEX);
          Serial1.print("spi_loop_5a=0x"); Serial1.println(got2, HEX);
          Serial1.print("spi_loopback=");
          Serial1.println((got == 0xA5 && got2 == 0x5A) ? "ok" : "absent");
        } else if (line == "spibadpins") {
          /* PA5 is SPI1 SCK but PB14 is only SPI2 MISO, so no single
             peripheral can serve the trio. That must be reported, not
             silently half-configured. */
          SPIClassCH32H4 bad(PA5, PB14, PA7);
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
