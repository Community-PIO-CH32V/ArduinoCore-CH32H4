/*
   SPI slave: echo what the master sent, on the next frame.

   This part has two SPI blocks brought out, so one board can be both ends:
   SPI (SPI1, PA5/PA6/PA7) is the master and SPISlave (SPI4, PE2/PE5/PE6 with
   chip select on PE4) is the slave. Four jumpers:

       PA5 - PE2    SCK
       PA7 - PE6    master MOSI  -> slave MOSI
       PA6 - PE5    master MISO <- slave MISO
       PE3 - PE4    chip select

   and the PA6-PA7 loopback jumper, if it is fitted, must come OUT: it shorts
   the master's MOSI onto its own MISO and fights the slave.

   THE ONE THING TO UNDERSTAND about an SPI slave: it cannot answer the frame
   it is being asked. The master supplies the clock, so by the time the slave
   has seen a byte it has already had to shift one out. setData() queues what
   the NEXT frame will send. Every SPI slave works this way; a sketch that
   expects a reply in the same transaction is the commonest way to be confused
   by one.

   This example code is in the public domain.

   ---
   Written for the CH32H41x core, in the shape of arduino-pico's SPISlave API.
*/

#include <SPI.h>
#include <SPISlave.h>

static const int CS = PE3;   /* the MASTER's chip-select output */

static volatile uint32_t frames = 0;
static uint8_t echo[16];
static volatile size_t echoLen = 0;

/* Runs in interrupt context when a frame ends. Queue the reply and leave. */
void onReceive(uint8_t *data, size_t len) {
  if (len > sizeof(echo)) {
    len = sizeof(echo);
  }
  memcpy(echo, data, len);
  echoLen = len;
  frames++;
  /* What the master will clock out of us NEXT time. */
  SPISlave.setData(echo, len);
}

void setup() {
  Serial.begin(115200);
  while (!Serial) {
    delay(10);
  }

  pinMode(CS, OUTPUT);
  digitalWrite(CS, HIGH);

  SPISlave.onDataRecv(onReceive);
  if (!SPISlave.begin(SPISettings(1000000, MSBFIRST, SPI_MODE0))) {
    Serial.println("SPISlave.begin() failed");
    while (1) {
      delay(1000);
    }
  }
  Serial.print("slave chip select is ");
  Serial.println(SPISlave.hardwareCS() ? "hardware (NSS)" : "software");

  SPI.begin();
  Serial.println("SPI slave echo -- see the wiring note at the top");
}

static void frame(const uint8_t *out, uint8_t *in, size_t len) {
  SPI.beginTransaction(SPISettings(1000000, MSBFIRST, SPI_MODE0));
  digitalWrite(CS, LOW);
  delayMicroseconds(20);        /* let the slave see the select */
  for (size_t i = 0; i < len; i++) {
    in[i] = SPI.transfer(out[i]);
  }
  digitalWrite(CS, HIGH);
  SPI.endTransaction();
  delay(2);                     /* let the slave's callback run */
}

void loop() {
  static uint8_t n = 0;
  uint8_t out[4] = { n, (uint8_t)(n + 1), (uint8_t)(n + 2), (uint8_t)(n + 3) };
  uint8_t in[4];

  frame(out, in, 4);

  Serial.print("sent ");
  for (int i = 0; i < 4; i++) {
    Serial.print(out[i]);
    Serial.print(' ');
  }
  /* The FIRST frame's reply is whatever the slave had queued before it saw
     anything -- zeros. From the second frame on, it is the previous frame. */
  Serial.print(" got back ");
  for (int i = 0; i < 4; i++) {
    Serial.print(in[i]);
    Serial.print(' ');
  }
  Serial.print(" frames=");
  Serial.println((int)frames);

  n += 4;
  delay(500);
}
