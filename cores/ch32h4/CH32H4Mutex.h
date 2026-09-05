/* Cross-core mutexes, and what already does not need one.
 *
 * READ THIS BEFORE REACHING FOR A LOCK. Most of what a sketch shares between
 * setup()/loop() and setup1()/loop1() is already safe:
 *
 *   Serial1 IS SAFE FROM BOTH CORES. USART1 is shared with the boot console,
 *   so every write takes a hardware semaphore -- once for a whole buffer, so a
 *   println() from one core cannot be cut in half by the other. Two cores both
 *   calling Serial1.println() interleave by line, not by character, and that
 *   costs you nothing to arrange.
 *
 *   The FIFO is safe. It is what it is for: CH32H4.fifo.push() on one core and
 *   pop() on the other need no lock of any kind.
 *
 *   Serial (USB CDC) IS THE V5F's. The USB device task refuses to run on the
 *   V3F -- two cores inside TinyUSB's event queue would corrupt it silently --
 *   so a V3F that prints to Serial queues bytes nothing will ever flush. Use
 *   Serial1 from the V3F.
 *
 * WHAT DOES NEED A LOCK: everything else two cores touch at once. Serial2 to
 * Serial8 are deliberately unlocked -- they belong to one sketch and a
 * semaphore on every byte would be a cost with nothing on the other side of
 * it. Wire, SPI, a shared struct, a shared FS handle: all yours to protect.
 *
 *     CH32H4Mutex bus;                 // picks a free semaphore
 *
 *     void loop() {                    // V5F
 *         CH32H4MutexGuard g(bus);
 *         Wire.beginTransmission(0x3C);
 *         ...
 *     }
 *     void loop1() {                   // V3F
 *         CH32H4MutexGuard g(bus);
 *         Wire.beginTransmission(0x40);
 *         ...
 *     }
 *
 * WHY NOT A PLAIN bool OR std::atomic. There is no coherent atomic between
 * these two cores: they have separate caches and no shared exclusive monitor,
 * so a compare-and-swap on one is invisible to the other. The HSEM block is
 * the hardware that does work -- it records the taking core's ID and refuses
 * anyone else. A memory flag here is not a slow lock, it is not a lock.
 */
#pragma once

#include <stdint.h>

#include "ch32h4_xcore.h"

/* A hardware semaphore, held as an object.
 *
 * RECURSIVE, per core. Taking one twice from the same core is counted rather
 * than deadlocking -- HSEM itself refuses the second take, so without the
 * count a lock() inside a lock() would spin until the watchdog. The other core
 * is still excluded throughout.
 */
class CH32H4Mutex {
public:
    /* Take the next free semaphore. There are twelve for sketches; when they
     * are gone, id() is CH32H4Mutex::NONE and lock() does nothing -- which is
     * checkable with valid() and is better than silently sharing one. */
    CH32H4Mutex();

    /* A specific one, when two images have to agree on which. Must be
     * CH32H4_HSEM_USER_FIRST or above: 0-3 belong to the core, and taking one
     * of those deadlocks against the console. */
    explicit CH32H4Mutex(uint8_t id);

    static const uint8_t NONE = 0xFF;

    bool valid() const { return _id != NONE; }
    uint8_t id() const { return _id; }

    /* Spins until it is ours. */
    void lock();

    /* Returns false rather than waiting. */
    bool tryLock();

    /* Only from the core that holds it, and once per lock() that succeeded. */
    void unlock();

private:
    uint8_t _id;
};

/* Lock for a scope, and release however the scope ends -- including a return
 * from the middle of it, which is the case that gets forgotten. */
class CH32H4MutexGuard {
public:
    explicit CH32H4MutexGuard(CH32H4Mutex &m) : _m(m) { _m.lock(); }
    ~CH32H4MutexGuard() { _m.unlock(); }

    CH32H4MutexGuard(const CH32H4MutexGuard &) = delete;
    CH32H4MutexGuard &operator=(const CH32H4MutexGuard &) = delete;

private:
    CH32H4Mutex &_m;
};
