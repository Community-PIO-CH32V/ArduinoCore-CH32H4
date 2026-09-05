#include "CH32H4Mutex.h"

/* The depth counting is in ch32h4_xcore.c, not here.
 *
 * It has to be: the counters live in .xcore, which is NOLOAD, so they must be
 * cleared before either core uses one -- and the only place that runs early
 * enough is ch32h4_xcore_init(), on the V3F before the V5F is awake. A
 * counter left holding whatever the SRAM came up with makes lock() believe it
 * already holds the semaphore, so it never takes it: a lock that compiles,
 * runs, costs nothing and protects nothing. This file had exactly that bug
 * until a test with two cores writing one sixteen-word object caught it.
 */

/* The next semaphore a default-constructed mutex will take.
 *
 * A plain static, deliberately: C++ static initialisation runs once, on the
 * V5F, before setup1() exists -- so two cores cannot be allocating at the same
 * time. Construct mutexes as globals, which is what one shared between cores
 * has to be anyway. */
static uint8_t s_next = CH32H4_HSEM_USER_FIRST;

CH32H4Mutex::CH32H4Mutex() {
    _id = (s_next < CH32H4_HSEM_COUNT) ? s_next++ : NONE;
}

CH32H4Mutex::CH32H4Mutex(uint8_t id) {
    /* Below USER_FIRST is the core's -- HSEM 0 is the console, and a sketch
     * that took it would deadlock against its own Serial1.print(). Refusing is
     * better than the ten minutes it would otherwise take to find. */
    _id = (id >= CH32H4_HSEM_USER_FIRST && id < CH32H4_HSEM_COUNT) ? id : NONE;
}

void CH32H4Mutex::lock() {
    if (_id != NONE) {
        ch32h4_mutex_lock_rec(_id);
    }
}

bool CH32H4Mutex::tryLock() {
    return (_id != NONE) && ch32h4_mutex_try_lock_rec(_id);
}

void CH32H4Mutex::unlock() {
    if (_id != NONE) {
        ch32h4_mutex_unlock_rec(_id);
    }
}
