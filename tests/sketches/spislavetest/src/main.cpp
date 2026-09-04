/* SPI master and SPI slave on the same chip, talking to each other.

   SPI1 is the master on PA5/PA6/PA7 and SPI4 the slave on PE2/PE5/PE6, with
   the master driving PE3 as a plain GPIO into the slave's NSS on PE4. Those
   eight pins are the whole of this part's 3.3 V rail, which is why this
   pairing and not another: a master on the VIO18 rail idles around 1.8 V and
   a 3.3 V input may not read that as a high, so the transfer would work at
   the register level and produce nothing on the wire.

   Bench wiring, four jumpers:
       PA5 -> PE2   SCK
       PA7 -> PE6   MOSI
       PA6 -> PE5   MISO
       PE3 -> PE4   CS to NSS

   The PA6-PA7 loopback jumper must be OUT. It shorts the master's MOSI to its
   own MISO, so the slave and the jumper would both be driving PA6.
*/
#include <Arduino.h>
#include <SPI.h>
#include <SPISlave.h>

#define CS_PIN PE3

static const size_t MAXN = 256;

/* Written by the slave's receive callback, which runs in interrupt context. */
static volatile bool s_frameDone = false;
static volatile size_t s_frameLen = 0;
static uint8_t s_frameBuf[MAXN];

static volatile uint32_t s_sentCount = 0;

static void onRecv(uint8_t *data, size_t len) {
  if (len > MAXN) {
    len = MAXN;
  }
  memcpy(s_frameBuf, data, len);
  s_frameLen = len;
  /* Last, and after the copy: loop() polls this flag and reads the buffer as
     soon as it is set, so setting it first would hand over a buffer that is
     still being written. */
  s_frameDone = true;
}

static void onSent(void) { s_sentCount++; }

/* One exchange. Returns false if the slave never reported a frame.

   Both directions are checked, because they fail differently: the master
   reading back what the slave queued proves MISO and the slave's transmit
   path, and the slave reporting what the master sent proves MOSI and its
   receive path. A test that only looked at one would pass with half the
   wiring. */
static bool exchange(size_t n, uint32_t hz, uint8_t mode,
                     uint8_t *masterRx, const uint8_t *masterTx,
                     const uint8_t *slaveTx) {
  s_frameDone = false;
  s_frameLen = 0;

  SPISlave.setData(slaveTx, n);

  digitalWrite(CS_PIN, LOW);
  /* A moment between the select and the first clock. The slave takes its
     select through hardware NSS, which enables the peripheral -- clocking
     immediately would race that against the pad. */
  delayMicroseconds(10);

  SPI.beginTransaction(SPISettings(hz, MSBFIRST, mode));
  SPI.transfer(masterTx, masterRx, n);
  SPI.endTransaction();

  delayMicroseconds(10);
  digitalWrite(CS_PIN, HIGH);

  /* The frame ends on the CS rise, which is an interrupt, so it has not
     necessarily been serviced by the time digitalWrite() returns. */
  const uint32_t deadline = millis() + 100;
  while (!s_frameDone && millis() < deadline) {
  }
  return s_frameDone;
}

void setup() {
  Serial1.begin(115200);

  pinMode(CS_PIN, OUTPUT);
  digitalWrite(CS_PIN, HIGH);

  SPI.begin();
  SPISlave.onDataRecv(onRecv);
  SPISlave.onDataSent(onSent);
  const bool slaveOk = SPISlave.begin(SPISettings(1000000, MSBFIRST, SPI_MODE0));

  Serial1.print("spis_master=");
  Serial1.println(SPI.peripheral());
  Serial1.print("spis_slave=");
  Serial1.println(slaveOk ? SPISlave.peripheral() : 0);
  Serial1.print("spis_hardcs=");
  Serial1.println(SPISlave.hardwareCS() ? 1 : 0);
  Serial1.println("spislavetest ready");
  Serial1.print("> ");
}

void loop() {
  static String line;
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) {
        if (line == "spisinfo") {
          Serial1.print("spis_master=");
          Serial1.println(SPI.peripheral());
          Serial1.print("spis_slave=");
          Serial1.println(SPISlave.peripheral());
          Serial1.print("spis_hardcs=");
          Serial1.println(SPISlave.hardwareCS() ? 1 : 0);

        } else if (line.startsWith("spisxfer ")) {
          /* "spisxfer <n> [hz] [mode]" -- one full-duplex frame of n bytes.

             The two patterns are deliberately different from each other and
             from anything a stuck bus would produce: all-zeroes and all-ones
             both look like a floating line, and a pattern that is the same in
             both directions cannot tell a crossed MISO/MOSI from a working
             one. */
          int sp1 = line.indexOf(' ');
          int sp2 = line.indexOf(' ', sp1 + 1);
          int sp3 = (sp2 < 0) ? -1 : line.indexOf(' ', sp2 + 1);
          const size_t n = (size_t)line.substring(sp1 + 1).toInt();
          const uint32_t hz = (sp2 < 0) ? 1000000u
                                        : (uint32_t)line.substring(sp2 + 1).toInt();
          const uint8_t mode = (sp3 < 0) ? SPI_MODE0
                                         : (uint8_t)line.substring(sp3 + 1).toInt();

          if (n == 0 || n > MAXN) {
            Serial1.println("spis_xfer=bad_length");
          } else {
            static uint8_t masterTx[MAXN], masterRx[MAXN], slaveTx[MAXN];
            for (size_t i = 0; i < n; i++) {
              masterTx[i] = (uint8_t)(0x40 + i * 3);
              slaveTx[i] = (uint8_t)(0x80 + i * 5);
            }
            memset(masterRx, 0, n);

            const uint32_t sentBefore = s_sentCount;
            const bool got = exchange(n, hz, mode, masterRx, masterTx, slaveTx);

            Serial1.print("spis_frame=");
            Serial1.println(got ? 1 : 0);
            Serial1.print("spis_rxlen=");
            Serial1.println((int)s_frameLen);
            /* Did the slave hear the master? */
            Serial1.print("spis_slave_got=");
            Serial1.println((got && s_frameLen == n &&
                             memcmp(s_frameBuf, masterTx, n) == 0) ? 1 : 0);
            /* Did the master hear the slave? */
            Serial1.print("spis_master_got=");
            Serial1.println(memcmp(masterRx, slaveTx, n) == 0 ? 1 : 0);
            Serial1.print("spis_sent_cb=");
            Serial1.println((int)(s_sentCount - sentBefore));
            /* First and last byte each way, so a failure says HOW it failed:
               a one-byte shift, a bit-order flip and a dead line all look the
               same through a pass/fail. */
            Serial1.print("spis_m0=0x"); Serial1.println(masterRx[0], HEX);
            Serial1.print("spis_mn=0x"); Serial1.println(masterRx[n - 1], HEX);
            Serial1.print("spis_s0=0x"); Serial1.println(s_frameBuf[0], HEX);
            Serial1.print("spis_sn=0x");
            Serial1.println(s_frameLen ? s_frameBuf[s_frameLen - 1] : 0, HEX);
          }

        } else if (line == "spisstale") {
          /* A frame the slave was given nothing for. It must not repeat the
             previous frame's data: stale bytes from two transfers ago are the
             failure mode that looks like a working link right up until the
             moment the timing shifts. */
          static uint8_t masterTx[8], masterRx[8], slaveTx[8];
          for (size_t i = 0; i < 8; i++) {
            masterTx[i] = (uint8_t)(0x11 + i);
            slaveTx[i] = (uint8_t)(0xE0 + i);
          }
          exchange(8, 1000000, SPI_MODE0, masterRx, masterTx, slaveTx);

          /* Second frame, nothing queued. */
          s_frameDone = false;
          memset(masterRx, 0, 8);
          digitalWrite(CS_PIN, LOW);
          delayMicroseconds(10);
          SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
          SPI.transfer(masterTx, masterRx, 8);
          SPI.endTransaction();
          delayMicroseconds(10);
          digitalWrite(CS_PIN, HIGH);
          const uint32_t deadline = millis() + 100;
          while (!s_frameDone && millis() < deadline) {
          }

          Serial1.print("spis_stale_frame=");
          Serial1.println(s_frameDone ? 1 : 0);
          Serial1.print("spis_stale_repeat=");
          Serial1.println(memcmp(masterRx, slaveTx, 8) == 0 ? 1 : 0);
          Serial1.print("spis_stale_slave_got=");
          Serial1.println((s_frameLen == 8 &&
                           memcmp(s_frameBuf, masterTx, 8) == 0) ? 1 : 0);

        } else if (line == "spisdeselect") {
          /* With CS never asserted the slave must stay silent. This is what
             hardware NSS buys, and if it is not actually wired to NSS the
             slave answers anyway -- which is exactly the failure this catches,
             since every other test here would still pass. */
          static uint8_t masterTx[8], masterRx[8], slaveTx[8];
          for (size_t i = 0; i < 8; i++) {
            masterTx[i] = (uint8_t)(0x21 + i);
            slaveTx[i] = (uint8_t)(0xC0 + i);
          }
          SPISlave.setData(slaveTx, 8);
          s_frameDone = false;
          memset(masterRx, 0, 8);

          digitalWrite(CS_PIN, HIGH);   /* deliberately NOT selected */
          SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
          SPI.transfer(masterTx, masterRx, 8);
          SPI.endTransaction();
          delay(5);

          Serial1.print("spis_desel_frame=");
          Serial1.println(s_frameDone ? 1 : 0);
          Serial1.print("spis_desel_answered=");
          Serial1.println(memcmp(masterRx, slaveTx, 8) == 0 ? 1 : 0);
          /* Clear anything the slave latched, so the next test starts clean. */
          SPISlave.end();
          SPISlave.begin(SPISettings(1000000, MSBFIRST, SPI_MODE0));

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
