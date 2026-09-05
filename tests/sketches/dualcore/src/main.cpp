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

/* CH32H4Mutex, under contention from both cores.
 *
 * The shared object is deliberately WIDER THAN A WORD and written one word at
 * a time: a struct that is only ever half-written by one core while the other
 * reads it is the failure a mutex exists to prevent, and a single word would
 * not show it because a 32-bit store is atomic on this part anyway.
 *
 * Every writer stamps the same counter value into all sixteen words. Any
 * reader that sees two different values has caught a torn write. With the
 * lock that must never happen; WITHOUT it, it must -- which is why the
 * unlocked mode exists. A test that only checks the locked case passes just as
 * well when the lock does nothing at all.
 */
static const int XWORDS = 16;
static volatile uint32_t xshared[XWORDS];
CH32H4Mutex xmutex;

static volatile bool xrun = false;
static volatile bool xlocked = true;
static volatile uint32_t xwrites[2];
static volatile uint32_t xreads[2];
static volatile uint32_t xtears[2];

static void xstamp(uint8_t core) {
  const uint32_t v = xwrites[core] + 1u;
  for (int i = 0; i < XWORDS; i++) {
    xshared[i] = v;
    /* Widen the window. Without this the whole store fits between two of the
       other core's instructions often enough that an unlocked run can look
       clean, and then the negative half of the test proves nothing. */
    __asm volatile("nop; nop; nop; nop");
  }
  xwrites[core] = v;
}

static void xcheck(uint8_t core) {
  const uint32_t first = xshared[0];
  for (int i = 1; i < XWORDS; i++) {
    if (xshared[i] != first) {
      xtears[core]++;
      break;
    }
  }
  xreads[core]++;
}

static void xwork(uint8_t core) {
  if (!xrun) {
    return;
  }
  if (xlocked) {
    CH32H4MutexGuard g(xmutex);
    xstamp(core);
    xcheck(core);
  } else {
    xstamp(core);
    xcheck(core);
  }
}

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
  xwork(0);
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

  } else if (cmd == "hsemraw") {
    /* What the read-to-lock register actually returns, step by step. */
    HSEM_ReleaseOneSem((HSEM_ID_TypeDef)6, 0);
    uint32_t r1 = HSEM->RLRX[6];   /* take from free */
    uint32_t r2 = HSEM->RLRX[6];   /* take when already ours */
    uint32_t rx = HSEM->RX[6];
    HSEM_ReleaseOneSem((HSEM_ID_TypeDef)6, 0);
    uint32_t r3 = HSEM->RLRX[6];   /* take from free again */
    HSEM_ReleaseOneSem((HSEM_ID_TypeDef)6, 0);
    Serial1.print("coreid="); Serial1.println(NVIC_GetCurrentCoreID());
    Serial1.print("r1=0x"); Serial1.println(r1, HEX);
    Serial1.print("r2=0x"); Serial1.println(r2, HEX);
    Serial1.print("rx=0x"); Serial1.println(rx, HEX);
    Serial1.print("r3=0x"); Serial1.println(r3, HEX);

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

  } else if (cmd == "mutexrecurse") {
    /* CH32H4Mutex is recursive per core: the raw HSEM refuses a second take
       from the same core, so without the depth count this would deadlock. */
    CH32H4Mutex m;
    Serial1.print("rec_valid="); Serial1.println(m.valid() ? 1 : 0);
    Serial1.print("rec_id="); Serial1.println(m.id());
    m.lock();
    m.lock();
    bool inner = m.tryLock();
    m.unlock();
    m.unlock();
    m.unlock();
    /* Fully released, so a fresh take must succeed. */
    bool after = m.tryLock();
    m.unlock();
    Serial1.print("rec_inner="); Serial1.println(inner ? 1 : 0);
    Serial1.print("rec_after="); Serial1.println(after ? 1 : 0);

  } else if (cmd.startsWith("xmutex ")) {
    /* Both cores hammering one wider-than-a-word object. */
    const String mode = cmd.substring(7);
    xlocked = (mode == "on");
    for (int i = 0; i < 2; i++) {
      xwrites[i] = 0;
      xreads[i] = 0;
      xtears[i] = 0;
    }
    for (int i = 0; i < XWORDS; i++) {
      xshared[i] = 0;
    }
    xrun = true;
    const uint32_t until = millis() + 1500;
    while (millis() < until) {
      xwork(1);
    }
    xrun = false;
    delay(20);          /* let the V3F finish whatever round it is in */

    Serial1.print("x_locked="); Serial1.println(xlocked ? 1 : 0);
    Serial1.print("x_v5f_writes="); Serial1.println((unsigned)xwrites[1]);
    Serial1.print("x_v3f_writes="); Serial1.println((unsigned)xwrites[0]);
    Serial1.print("x_v5f_reads="); Serial1.println((unsigned)xreads[1]);
    Serial1.print("x_v3f_reads="); Serial1.println((unsigned)xreads[0]);
    Serial1.print("x_tears="); Serial1.println((unsigned)(xtears[0] + xtears[1]));
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
