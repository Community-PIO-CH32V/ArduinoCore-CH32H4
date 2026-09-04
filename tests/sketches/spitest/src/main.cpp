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
        } else if (line.startsWith("spiblock ")) {
          /* Block transfers, on both sides of the DMA threshold, in place and
             into a separate buffer. The same PA6-PA7 jumper: what goes out
             must come back, whichever path carries it.

             In place is the interesting one. The DMA transmit channel reads a
             byte before the receive channel writes over it -- the transmit
             side stays two byte-times ahead -- so no copy is needed. If that
             were wrong the buffer would come back with its own tail. */
          const size_t n = (size_t)line.substring(9).toInt();
          static uint8_t tx[1024], rx[1024];
          if (n == 0 || n > sizeof(tx)) {
            Serial1.println("spi_block=bad_length");
          } else {
            for (size_t i = 0; i < n; i++) { tx[i] = (uint8_t)(n + i * 7); }
            memcpy(rx, tx, n);
            SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
            SPI.transfer(rx, n);
            SPI.endTransaction();
            Serial1.print("spi_inplace=");
            Serial1.println(memcmp(tx, rx, n) == 0 ? 1 : 0);

            memset(rx, 0, n);
            SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
            SPI.transfer(tx, rx, n);
            SPI.endTransaction();
            Serial1.print("spi_split=");
            Serial1.println(memcmp(tx, rx, n) == 0 ? 1 : 0);
          }
        } else if (line == "spihalfduplex") {
          /* Null rx must not hang, and null tx must clock 0xFF out -- which
             the jumper returns. A send-only DMA transfer that does not wait
             for the bus to go idle would also return with a byte still on the
             wire. */
          static uint8_t buf[256];
          for (size_t i = 0; i < sizeof(buf); i++) { buf[i] = (uint8_t)i; }
          SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
          const uint32_t t0 = micros();
          SPI.transfer(buf, (void *)nullptr, sizeof(buf));
          const uint32_t us = micros() - t0;
          SPI.endTransaction();
          Serial1.print("spi_txonly_us="); Serial1.println(us);

          memset(buf, 0, 64);
          SPI.beginTransaction(SPISettings(8000000, MSBFIRST, SPI_MODE0));
          SPI.transfer((const void *)nullptr, buf, 64);
          SPI.endTransaction();
          bool ff = true;
          for (int i = 0; i < 64; i++) { if (buf[i] != 0xFF) { ff = false; } }
          Serial1.print("spi_rxonly_ff="); Serial1.println(ff ? 1 : 0);

        } else if (line.startsWith("spibench ")) {
          /* Throughput, against the clock the divider can actually produce --
             a power of two below HCLK, not the number asked for. */
          const uint32_t hz = (uint32_t)line.substring(9).toInt();
          static uint8_t buf[4096];
          SPI.beginTransaction(SPISettings(hz, MSBFIRST, SPI_MODE0));
          const uint32_t t0 = micros();
          SPI.transfer(buf, sizeof(buf));
          const uint32_t us = micros() - t0;
          SPI.endTransaction();
          Serial1.print("spi_bench_us="); Serial1.println(us);
          Serial1.print("spi_bench_kbits=");
          Serial1.println(us ? (uint32_t)((uint64_t)sizeof(buf) * 8000 / us) : 0);

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
