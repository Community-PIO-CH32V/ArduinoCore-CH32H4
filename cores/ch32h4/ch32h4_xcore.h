/* State and messaging shared between the two cores.
 *
 * Everything here lives in XCORE_RAM, in the shared region. That is not a
 * preference: the shared region is the only memory both cores reach at speed,
 * and the V5F's instruction cache is not coherent with anything else. It also
 * cannot live in .bss, because the V3F zeroes .bss before the V5F is even
 * awake, so anything the V5F wrote there would be destroyed.
 *
 * The section is NOLOAD, so nothing initialises it at reset. The V3F clears
 * what it needs explicitly before waking the V5F.
 *
 * The two cores:
 *   V5F  core 1, 400 MHz, out-of-order, 32 KB I-cache. Runs setup()/loop().
 *   V3F  core 0, 100 MHz, in-order. Boots first, brings the part up, wakes the
 *        V5F, then runs setup1()/loop1() if the sketch defines them.
 */
#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CH32H4_XCORE  __attribute__((section(".xcore")))

/* A magic value rather than 1, because the region is uninitialised at reset
 * and a stale or random word must not read as "ready". */
#define CH32H4_RUNTIME_READY_MAGIC  0x5F5FA5A5u

/* Set by the V5F once .init_array has run and the C++ runtime is usable. The
 * V3F waits for this before calling setup1(): a sketch's globals are
 * constructed on the V5F, and setup1() touching one before then would read an
 * unconstructed object. */
extern volatile uint32_t ch32h4_runtime_ready;

/* ---- The inter-core FIFO ------------------------------------------------
 *
 * One ring per direction, in shared RAM.
 *
 * The hardware IPC block is a doorbell with four message words, not a queue,
 * so it cannot be the FIFO on its own -- but it is what lets a waiting core
 * sleep instead of spinning, so it is used as the wakeup.
 *
 * Each ring has exactly one producer and one consumer, which is what makes it
 * safe without a lock: `head` is written only by the pushing core and `tail`
 * only by the popping core, and each reads the other's index without writing
 * it. Adding a second producer to either direction would break that, so do not.
 */
#define CH32H4_FIFO_DEPTH  8u   /* power of two: the wrap is a mask */

typedef struct {
    volatile uint32_t buf[CH32H4_FIFO_DEPTH];
    volatile uint32_t head;   /* written by the producer only */
    volatile uint32_t tail;   /* written by the consumer only */
} ch32h4_fifo_t;

/* Clears both rings. Called by the V3F before the wake, when it is the only
 * core running -- which is the only moment this is safe. */
void ch32h4_xcore_init(void);

/* Push a word to the other core. Blocking; spins while the ring is full. */
void ch32h4_fifo_push(uint32_t value);

/* Non-blocking push. False if the ring is full. */
bool ch32h4_fifo_push_nb(uint32_t value);

/* Pop a word sent to this core. Blocking; spins while empty. */
uint32_t ch32h4_fifo_pop(void);

/* Non-blocking pop. False if the ring is empty. */
bool ch32h4_fifo_pop_nb(uint32_t *value);

/* How many words are waiting for this core. */
int ch32h4_fifo_available(void);

/* Drop everything addressed to this core. */
void ch32h4_fifo_drain(void);

/* 0 on the V3F, 1 on the V5F. */
uint8_t ch32h4_core_num(void);

/* ---- Mutexes ------------------------------------------------------------
 *
 * Backed by the HSEM block, which is a hardware semaphore with core and
 * process IDs -- so a lock genuinely excludes the other core, which a plain
 * memory flag on a part with no coherent atomics would not.
 *
 * IDs 0..3 are reserved for the core; a sketch should use 4 and up.
 */
#define CH32H4_HSEM_COUNT  16u

bool ch32h4_mutex_try_lock(uint8_t id);
void ch32h4_mutex_lock(uint8_t id);
void ch32h4_mutex_unlock(uint8_t id);

#ifdef __cplusplus
}
#endif
