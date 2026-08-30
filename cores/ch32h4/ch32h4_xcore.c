#include "Arduino.h"
#include "ch32h4_xcore.h"

/* One ring per direction. Named for the core that READS them, because that is
 * what the code below has to reason about: this core pops from its own and
 * pushes to the other's. */
static ch32h4_fifo_t s_to_v3f CH32H4_XCORE;
static ch32h4_fifo_t s_to_v5f CH32H4_XCORE;

uint8_t ch32h4_core_num(void) {
    return (uint8_t)NVIC_GetCurrentCoreID();
}

void ch32h4_xcore_init(void) {
    /* Called by the V3F before the wake, when it is the only core running.
     * XCORE_RAM is NOLOAD, so without this both rings start with whatever the
     * region happened to contain and the first pop returns garbage. */
    s_to_v3f.head = s_to_v3f.tail = 0;
    s_to_v5f.head = s_to_v5f.tail = 0;
}

static ch32h4_fifo_t *rx_ring(void) {
    return (ch32h4_core_num() == 0) ? &s_to_v3f : &s_to_v5f;
}

static ch32h4_fifo_t *tx_ring(void) {
    return (ch32h4_core_num() == 0) ? &s_to_v5f : &s_to_v3f;
}

/* Ring the other core's doorbell, so a core blocked in ch32h4_fifo_pop() can
 * stop spinning. The FIFO is correct without this; it is only about power and
 * latency. */
static void ring_doorbell(void) {
    IPC_SetFlagStatus(ch32h4_core_num() == 0 ? IPC_CH0 : IPC_CH1,
                      IPC_CH_Sta_Bit0);
}

bool ch32h4_fifo_push_nb(uint32_t value) {
    ch32h4_fifo_t *r = tx_ring();
    const uint32_t head = r->head;
    const uint32_t next = (head + 1u) & (CH32H4_FIFO_DEPTH - 1u);
    if (next == r->tail) {
        return false;   /* full */
    }
    r->buf[head] = value;
    /* The data must be visible before the index that publishes it. Both cores
     * see the shared region without a data cache between them, but the
     * compiler is free to reorder two volatile-qualified stores relative to
     * ordinary code, so the barrier is what makes the order a promise. */
    __asm volatile("fence w, w" ::: "memory");
    r->head = next;
    ring_doorbell();
    return true;
}

void ch32h4_fifo_push(uint32_t value) {
    while (!ch32h4_fifo_push_nb(value)) {
        /* Spin. Not WFI: the other core drains this ring, and nothing
         * guarantees an interrupt on this one when it does. */
    }
}

bool ch32h4_fifo_pop_nb(uint32_t *value) {
    ch32h4_fifo_t *r = rx_ring();
    const uint32_t tail = r->tail;
    if (tail == r->head) {
        return false;   /* empty */
    }
    const uint32_t v = r->buf[tail];
    __asm volatile("fence r, r" ::: "memory");
    r->tail = (tail + 1u) & (CH32H4_FIFO_DEPTH - 1u);
    if (value) {
        *value = v;
    }
    return true;
}

uint32_t ch32h4_fifo_pop(void) {
    uint32_t v = 0;
    while (!ch32h4_fifo_pop_nb(&v)) {
    }
    return v;
}

int ch32h4_fifo_available(void) {
    ch32h4_fifo_t *r = rx_ring();
    return (int)((r->head - r->tail) & (CH32H4_FIFO_DEPTH - 1u));
}

void ch32h4_fifo_drain(void) {
    ch32h4_fifo_t *r = rx_ring();
    r->tail = r->head;
}

/* ---- Mutexes ------------------------------------------------------------ */

static bool hsem_ready = false;

static void hsem_setup(void) {
    if (hsem_ready) {
        return;
    }
    /* HSEM sits on the core-private bus at 0xE000C000 rather than on HB/HB1/
     * HB2, so there is no RCC clock to enable for it. */
    hsem_ready = true;
}

bool ch32h4_mutex_try_lock(uint8_t id) {
    if (id >= CH32H4_HSEM_COUNT) {
        return false;
    }
    hsem_setup();
    /* FastTake is the two-step take collapsed into one read: the hardware
     * records this core as the owner if the semaphore was free. A read-check-
     * write in software would have exactly the race the block exists to
     * remove. */
    /* The SDK's ErrorStatus is {NoREADY, READY}, not the {ERROR, SUCCESS} the
     * name suggests. */
    return HSEM_FastTake((HSEM_ID_TypeDef)id) == READY;
}

void ch32h4_mutex_lock(uint8_t id) {
    while (!ch32h4_mutex_try_lock(id)) {
    }
}

void ch32h4_mutex_unlock(uint8_t id) {
    if (id >= CH32H4_HSEM_COUNT) {
        return;
    }
    /* Process ID 0 throughout: this core does not use HSEM's process
     * distinction, only its core distinction. */
    HSEM_ReleaseOneSem((HSEM_ID_TypeDef)id, 0);
}
