/* The `CH32H4` object: the second core, and the things that need both.
 *
 * Modelled on arduino-pico's `rp2040`, so a sketch written for that reads the
 * same way here.
 *
 *   setup()  / loop()   run on the V5F -- 400 MHz, out-of-order, I-cache.
 *   setup1() / loop1()  run on the V3F -- 100 MHz, in-order.
 *
 * That is the opposite assignment from arduino-pico, where setup() runs on the
 * boot core. Here the boot core is the slow one: the V3F brings the part up
 * and wakes the V5F, and a sketch that says nothing about cores should get the
 * fast one.
 */
#pragma once

#include <Arduino.h>
#include "ch32h4_xcore.h"
#include "CH32H4Mutex.h"

class CH32H4FIFO {
public:
    /* Blocking. Spins while the ring is full; the other core drains it. */
    void push(uint32_t v) { ch32h4_fifo_push(v); }
    bool push_nb(uint32_t v) { return ch32h4_fifo_push_nb(v); }

    /* Blocking. Spins while empty. */
    uint32_t pop() { return ch32h4_fifo_pop(); }
    bool pop_nb(uint32_t *v) { return ch32h4_fifo_pop_nb(v); }

    /* Words waiting for THIS core. */
    int available() { return ch32h4_fifo_available(); }
    void drain() { ch32h4_fifo_drain(); }
};

class CH32H4Core {
public:
    /* 0 on the V3F, 1 on the V5F. */
    uint8_t getCoreNum() { return ch32h4_core_num(); }

    /* The clock this core actually runs at: 400 MHz on the V5F, 100 MHz on the
     * V3F. Not the same as the bus clock, which is 100 MHz for both and is
     * what every peripheral divider uses. */
    uint32_t getCpuFreqHz() { return SystemCoreClock; }

    /* The bus clock. This is the one to divide for a peripheral. */
    uint32_t getBusFreqHz() { return ch32h4_hclk(); }

    /* Free heap across both regions -- DTCM first, then the shared half. */
    size_t getFreeHeap() { return ch32h4_heap_free(); }

    /* The chip's unique ID, 8 bytes. */
    void getUniqueId(uint8_t *out) {
        const uint8_t *id = (const uint8_t *)0x1FFFF7E8;
        for (int i = 0; i < 8; i++) { out[i] = id[i]; }
    }

    /* Hardware semaphores, raw. CH32H4Mutex is the one to use -- it is
     * recursive, it allocates its own id, and it has a scope guard. These
     * three are here for code that needs a specific numbered semaphore.
     * IDs 0-3 belong to the core; use CH32H4_HSEM_USER_FIRST and up. */
    bool mutexTryLock(uint8_t id) { return ch32h4_mutex_try_lock(id); }
    void mutexLock(uint8_t id) { ch32h4_mutex_lock(id); }
    void mutexUnlock(uint8_t id) { ch32h4_mutex_unlock(id); }

    CH32H4FIFO fifo;
};

extern CH32H4Core CH32H4;

/* arduino-pico spells this `rp2040`. Sketches ported from there mostly want
 * getCoreNum() and fifo, both of which are here under the same names. */
