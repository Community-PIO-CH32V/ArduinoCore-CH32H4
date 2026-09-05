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

#include <Adafruit_SleepyDog.h>

extern "C" {
#include "ch32h4_flash.h"
#include "ch32h4_fault.h"
#include "ch32h4_itcm.h"
#include "ch32h4_xcore.h"
}

static volatile uint32_t core1Iterations = 0;
static volatile uint32_t core1Echoed = 0;

/* THE QUESTION THIS SKETCH EXISTS TO ANSWER, for OTA:
 *
 * What happens to the V3F while the V5F erases and programs flash? Both cores
 * execute XIP from the same array. The V5F's programming loop is in ITCM with
 * interrupts masked, which is what makes it safe FOR THE V5F. The V3F is not
 * covered by any of that: it is running loop1() out of flash and taking
 * interrupts.
 *
 * Three things could be true and they lead to different OTA designs:
 *   1. the controller stalls reads across the whole array -- the V3F stalls
 *      and resumes, and nothing is wrong;
 *   2. it stalls only the bank being written -- nothing happens at all;
 *   3. a read during a program returns garbage -- the V3F executes garbage,
 *      and the failure is a hard fault on the other core in the middle of an
 *      update.
 *
 * The counter below is the witness. It is incremented from loop1(), which is
 * ordinary flash-resident code, and a fault on the V3F is recorded in .xcore
 * and survives the reset. If the counter advances across a 128 KB erase and
 * program and no fault is logged, (3) is out and OTA does not need to park
 * the V3F.
 *
 * THE REGION. 0x08060000 is 352 KB into the 912 KB sketch area -- far past
 * this ~31 KB image, and far below the EEPROM at 0x080EC000. Nothing lives
 * there, and this sketch declares no filesystem, so nothing will.
 */
static const uint32_t STRESS_ADDR = 0x08060000u;
static const uint32_t STRESS_LEN  = 128u * 1024u;

/* PARKING THE V3F.
 *
 * The V5F has an instruction cache; the V3F is in-order and has none, so it
 * fetches every instruction from flash as it goes. ch32h4_flash_erase() calls
 * the SDK's FLASH_ErasePage(), which is NOT in ITCM -- so the erasing core is
 * itself executing from the array it is erasing, and gets away with it because
 * its cache holds the loop. The V3F has no such protection.
 *
 * park() is the answer: an ITCM-resident spin loop, so the V3F touches no
 * flash at all while the other core writes it. The counter proves it is still
 * running and the ack proves it got there before the erase started -- setting
 * a flag and hoping would leave a race exactly where it must not be.
 */
static volatile uint32_t s_parkReq CH32H4_XCORE;
static volatile uint32_t s_parkAck CH32H4_XCORE;
static volatile uint32_t s_parkSpins CH32H4_XCORE;

__itcm_func static void v3f_park_spin(void) {
    s_parkAck = 1u;
    while (s_parkReq) {
        s_parkSpins++;
    }
    s_parkAck = 0u;
}

/* Ask the V3F to park, and wait until it says it has. False means it never
   got there, and nothing that follows would be safe. */
static bool parkV3F(uint32_t timeout_ms) {
    s_parkSpins = 0;
    s_parkAck = 0;
    s_parkReq = 1;
    const uint32_t deadline = millis() + timeout_ms;
    while (!s_parkAck && millis() < deadline) {
    }
    return s_parkAck != 0u;
}

static void unparkV3F() {
    s_parkReq = 0;
    delay(2);
}

/* Every step prints BEFORE it runs and flushes, so if the board stops the last
   line on the wire says which operation did it. That is the whole design of
   this test: the informative outcome is the hang. */
static void step(const char *what) {
    Serial1.print("fs_step=");
    Serial1.println(what);
    Serial1.flush();
}

static void flashTest(const String &arg) {
    const bool park = arg.endsWith(" parked");
    String what = arg;
    if (park) {
        what = arg.substring(0, arg.length() - 7);
    }

    /* THE WATCHDOG IS WHY THIS IS SAFE TO RUN. If the V3F executes garbage
       and wedges, the board resets in about four seconds and the V3F prints
       its fault record on the next boot -- instead of hanging with the flash
       controller busy, which is what leaves the debug probe throwing 0x55 and
       needs a physical rescue. Learned the hard way. */
    Watchdog.enable(4000);

    if (park && !parkV3F(1000)) {
        Serial1.println("fs_park=failed");
        Watchdog.reset();
        return;
    }
    Serial1.print("fs_parked="); Serial1.println(park ? 1 : 0);
    Serial1.flush();

    const uint32_t before = core1Iterations;
    bool ok = true;

    if (what == "erase1") {
        step("erase-one-page");
        ok = ch32h4_flash_erase(STRESS_ADDR, 8192u);
        Watchdog.reset();

    } else if (what == "write1") {
        step("erase-one-page");
        ok = ch32h4_flash_erase(STRESS_ADDR, 8192u);
        Watchdog.reset();
        if (ok) {
            static uint8_t page[256];
            for (uint32_t i = 0; i < sizeof(page); i++) {
                page[i] = (uint8_t)(i * 31u);
            }
            step("program-one-page");
            ok = ch32h4_flash_write(STRESS_ADDR, page, sizeof(page));
            Watchdog.reset();
        }

    } else if (what == "big") {
        step("erase-128k");
        ok = ch32h4_flash_erase(STRESS_ADDR, STRESS_LEN);
        Watchdog.reset();
        if (ok) {
            static uint8_t page[256];
            step("program-128k");
            for (uint32_t off = 0; ok && off < STRESS_LEN; off += sizeof(page)) {
                for (uint32_t i = 0; i < sizeof(page); i++) {
                    page[i] = (uint8_t)((off + i) * 31u);
                }
                ok = ch32h4_flash_write(STRESS_ADDR + off, page, sizeof(page));
                if ((off & 0x3FFFu) == 0u) {
                    Watchdog.reset();
                }
            }
            Watchdog.reset();
        }

    } else {
        Serial1.println("fs_step=unknown");
        Watchdog.reset();
        if (park) {
            unparkV3F();
        }
        return;
    }

    step("done");
    const uint32_t after = core1Iterations;
    const uint32_t spins = s_parkSpins;

    if (park) {
        unparkV3F();
    }

    /* Verify what was written, when something was. */
    uint32_t bad = 0;
    if (ok && what != "erase1") {
        const uint32_t len = (what == "big") ? STRESS_LEN : 256u;
        for (uint32_t off = 0; off < len; off += 256u) {
            const uint8_t *p = (const uint8_t *)(uintptr_t)(STRESS_ADDR + off);
            for (uint32_t i = 0; i < 256u; i += 37u) {
                const uint8_t want = (what == "big")
                                     ? (uint8_t)((off + i) * 31u)
                                     : (uint8_t)(i * 31u);
                if (p[i] != want) {
                    bad++;
                    break;
                }
            }
        }
    }

    Serial1.print("fs_ok="); Serial1.println(ok ? 1 : 0);
    Serial1.print("fs_verify_bad="); Serial1.println((unsigned)bad);
    Serial1.print("fs_v3f_loops="); Serial1.println((unsigned)(after - before));
    Serial1.print("fs_v3f_spins="); Serial1.println((unsigned)spins);
    Serial1.print("fs_v3f_fault=0x");
    Serial1.println(ch32h4_fault_log_v3f.magic, HEX);
    Watchdog.reset();
}

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
  /* Runs on the V3F, after the V5F has finished constructing globals.
   *
   * EVERYTHING IN .xcore HAS TO BE INITIALISED HERE. That section is NOLOAD:
   * nothing zeroes it, so on a cold boot it holds whatever the SRAM came up
   * with and on a warm one it holds the previous run's values. A stale
   * s_parkReq means the V3F parks itself on its first loop1(), before anyone
   * asked -- which presents as core1_delta=0 and a park request that times
   * out, because the core is already inside the spin and will not
   * acknowledge a second time.
   *
   * This core does it rather than the V5F because loop1() may run before
   * setup() gets there. */
  core1Iterations = 0;
  s_parkReq = 0;
  s_parkAck = 0;
  s_parkSpins = 0;
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

  /* Park on request, in ITCM, so the other core can write flash. */
  if (s_parkReq) {
    v3f_park_spin();
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

  } else if (cmd.startsWith("flash ")) {
    flashTest(cmd.substring(6));
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
