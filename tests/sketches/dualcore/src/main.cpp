/* Both cores, in the shape arduino-pico's Multicore example uses.
 *
 * setup()/loop() run on the V5F at 400 MHz; setup1()/loop1() on the V3F at
 * 100 MHz. The FIFO carries words between them.
 *
 * The test protocol runs on Serial1 from the V5F only: two cores writing one
 * UART would interleave mid-character, and the resulting mess is exactly what
 * a boot loop looks like.
 */
#include <Arduino.h>

static volatile uint32_t core1Iterations = 0;
static volatile uint32_t core1Echoed = 0;

void setup() {
  Serial1.begin(115200);
  Serial1.print("core0_num=");
  Serial1.println(CH32H4.getCoreNum());
  Serial1.println("dualcore ready");
  Serial1.print("> ");
}

void setup1() {
  /* Runs on the V3F, after the V5F has finished constructing globals. */
  core1Iterations = 0;
}

void loop1() {
  core1Iterations++;
  /* Echo anything core 0 sends, with a marker, so the round trip proves both
     directions rather than just that the ring works locally. */
  uint32_t v;
  if (CH32H4.fifo.pop_nb(&v)) {
    core1Echoed++;
    CH32H4.fifo.push(v ^ 0xFFFFFFFFu);
  }
}

static void handle(const String &cmd) {
  if (cmd == "coreinfo") {
    Serial1.print("core0_num="); Serial1.println(CH32H4.getCoreNum());
    Serial1.print("core0_hz="); Serial1.println(CH32H4.getCpuFreqHz());
    Serial1.print("bus_hz="); Serial1.println(CH32H4.getBusFreqHz());

  } else if (cmd == "core1alive") {
    /* The V3F increments this in loop1(). If it moves, the second core is
       genuinely running the sketch's code. */
    uint32_t a = core1Iterations;
    delay(50);
    uint32_t b = core1Iterations;
    Serial1.print("core1_iterations_moved=");
    Serial1.println((b > a) ? 1 : 0);
    Serial1.print("core1_delta=");
    Serial1.println(b - a);

  } else if (cmd == "fifotest") {
    CH32H4.fifo.drain();
    CH32H4.fifo.push(0x12345678u);
    uint32_t got = 0;
    uint32_t guard = 0;
    while (!CH32H4.fifo.pop_nb(&got) && ++guard < 2000000u) { }
    Serial1.print("fifo_sent=0x12345678");
    Serial1.println();
    Serial1.print("fifo_got=0x"); Serial1.println(got, HEX);
    Serial1.print("fifo_roundtrip=");
    Serial1.println((got == (0x12345678u ^ 0xFFFFFFFFu)) ? "ok" : "FAIL");
    Serial1.print("core1_echoed="); Serial1.println(core1Echoed);

  } else if (cmd == "fifodepth") {
    CH32H4.fifo.drain();
    int pushed = 0;
    while (CH32H4.fifo.push_nb((uint32_t)pushed) && pushed < 32) { pushed++; }
    Serial1.print("fifo_pushed_before_full=");
    Serial1.println(pushed);

  } else if (cmd == "mutextest") {
    /* Hardware semaphore: taking it twice from the same core must still be
       exclusive -- HSEM records the owner, so a second FastTake fails. */
    bool first = CH32H4.mutexTryLock(4);
    bool second = CH32H4.mutexTryLock(4);
    CH32H4.mutexUnlock(4);
    bool third = CH32H4.mutexTryLock(4);
    CH32H4.mutexUnlock(4);
    Serial1.print("mutex_first="); Serial1.println(first ? 1 : 0);
    Serial1.print("mutex_second="); Serial1.println(second ? 1 : 0);
    Serial1.print("mutex_after_unlock="); Serial1.println(third ? 1 : 0);
  }
  Serial1.print("> ");
}

void loop() {
  static String line;
  while (Serial1.available()) {
    char c = (char)Serial1.read();
    if (c == '\n' || c == '\r') {
      if (line.length()) { handle(line); line = ""; }
    } else {
      line += c;
    }
  }
  yield();
}
