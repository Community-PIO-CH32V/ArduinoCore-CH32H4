/*
   Sharing a peripheral between the two cores.

   Serial1 and the FIFO are already safe from both cores and need none of
   this. Everything else -- Wire, SPI, Serial2 to Serial8, a shared struct, a
   file handle -- is yours to protect, and this is how.

   This example code is in the public domain.

   ---
   For the CH32H41x core.

   WHY NOT A bool, OR std::atomic. There is no coherent atomic between these
   two cores: separate caches, no shared exclusive monitor, so a
   compare-and-swap on one is invisible to the other. A memory flag here is
   not a slow lock, it is not a lock. CH32H4Mutex is the HSEM block, which
   records the taking core's ID in hardware and refuses anyone else.

   CH32H4Mutex is RECURSIVE per core: taking it twice from the same core is
   counted rather than deadlocking. The other core is still excluded
   throughout.

   CH32H4MutexGuard takes it for a scope and releases however the scope ends,
   including a return from the middle -- which is the case that gets
   forgotten.
*/

#include <CH32H4.h>
#include <Wire.h>

/* A global, because a mutex shared between cores has to be one. The default
   constructor takes the next free hardware semaphore; there are twelve. */
CH32H4Mutex i2cBus;

/* Something bigger than a word, so the guard has a reason to exist: two cores
   writing this without a lock produce a struct that was never written. */
struct Reading {
  uint32_t core;
  uint32_t millis;
  uint32_t value;
};
static volatile Reading last;

static void record(uint32_t value) {
  CH32H4MutexGuard g(i2cBus);          /* held until this function returns */
  last.core = CH32H4.getCoreNum();
  last.millis = millis();
  last.value = value;
}

void setup() {
  Serial1.begin(115200);
  delay(200);

  if (!i2cBus.valid()) {
    Serial1.println("no free hardware semaphore!");
  } else {
    Serial1.print("using hardware semaphore ");
    Serial1.println(i2cBus.id());
  }

  Wire.begin();
}

void loop() {
  {
    /* The bus, for one transaction. Nothing else may touch Wire inside here,
       on either core. */
    CH32H4MutexGuard g(i2cBus);
    Wire.beginTransmission(0x3C);
    Wire.write((uint8_t)0x00);
    Wire.endTransmission();
  }

  record(analogRead(A0));

  /* Reading the struct needs the lock too -- otherwise this can see half of
     the other core's write. */
  {
    CH32H4MutexGuard g(i2cBus);
    Serial1.print("last written by core ");
    Serial1.print((unsigned)last.core);
    Serial1.print(" at ");
    Serial1.print((unsigned)last.millis);
    Serial1.print(" ms, value ");
    Serial1.println((unsigned)last.value);
  }

  delay(1000);
}

void setup1() {
}

void loop1() {
  /* tryLock() when there is something else worth doing rather than waiting. */
  if (i2cBus.tryLock()) {
    Wire.beginTransmission(0x40);
    Wire.write((uint8_t)0x01);
    Wire.endTransmission();
    i2cBus.unlock();
  }

  record(millis() & 0xFFF);
  delay(700);
}
