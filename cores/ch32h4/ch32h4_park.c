/* Parking the other core, so this one can write flash.
 *
 * WHY THIS EXISTS. Both cores execute XIP from the same flash array, and only
 * one of them survives that array going busy. The V5F has an instruction cache
 * and flash_program_page() is in ITCM besides, so it keeps running while the
 * controller is programming. The V3F is in-order with no instruction cache: it
 * fetches every instruction it executes from the flash being written, and a
 * page program with the V3F running DOES NOT COMPLETE. The board hangs, and
 * only the watchdog gets it back. docs/hazards.md has the measurements.
 *
 * That is not only an OTA problem. A dualcore sketch writing LittleFS or
 * EEPROM from loop() while loop1() runs hits it today.
 *
 * HOW: BY INTERRUPT, so that it works whatever the other core is doing.
 *
 * A flag the other core polls was the first attempt and it is not good enough:
 * loop1() belongs to the sketch and may run for a long time without returning
 * or yielding, and a park that quietly fails turns a LittleFS write into a
 * failure the sketch never asked for.
 *
 * Two things make the interrupt route work on the V3F, and both had to be
 * checked rather than assumed:
 *
 *   - that core does have interrupts. startup_v3f.S programs mtvec and
 *     mstatus; its vector table was simply sixty copies of the stray handler
 *     because nothing had needed one. Slots 18 and 19 are real now.
 *
 *   - ITS VECTOR TABLE IS IN SRAM, not flash -- V3F_VECTOR at 0x2010D000. So
 *     taking the interrupt does not need a flash read, which matters because
 *     the whole point is to be safe while flash is busy. Had the table been
 *     in flash this could not work at all.
 *
 * The handler spins in ITCM with interrupts masked. Both are required: a
 * SysTick landing on the parked core would fetch ITS vector from SRAM but its
 * handler from flash, which is exactly what must not happen.
 */
#include "ch32h4_park.h"

#include "ch32h4_irq.h"
#include "ch32h4_itcm.h"
#include "ch32h4_xcore.h"
#include "ch32h417.h"

extern uint32_t millis(void);

/* Two channels, one per direction. CH0 and CH1 are the FIFO's doorbell.
 *
 *   CH2  park the V3F
 *   CH3  park the V5F
 */
#define PARK_CH_V3F  2u
#define PARK_CH_V5F  3u
#define PARK_MASK(ch)   (1u << ((ch) * 8u))

/* Indexed by the core being parked: [0] is the V3F, [1] the V5F.
 *
 * In .xcore, and cleared by ch32h4_xcore_init() -- that section is NOLOAD, and
 * a request flag holding whatever the SRAM came up with would park a core at
 * boot before anything asked. That has been hit twice; it is not theoretical. */
volatile uint32_t ch32h4_park_req[2] CH32H4_XCORE;
volatile uint32_t ch32h4_park_ack[2] CH32H4_XCORE;

/* Set by a core once it is running code that fetches from flash. The V3F only
 * does when the sketch defines setup1()/loop1(); otherwise it sleeps in stop
 * mode, fetches nothing, and needs no parking. */
volatile uint32_t ch32h4_park_live[2] CH32H4_XCORE;

/* The spin, in ITCM. Everything it touches is a register or a word of shared
 * RAM; it calls nothing, so nothing can drag it back into flash. */
__itcm_func static void park_spin(uint8_t self, uint32_t ch) {
    uint32_t prev;
    __asm volatile("csrrci %0, mstatus, 8" : "=r"(prev));

    ch32h4_park_ack[self] = 1u;
    while (ch32h4_park_req[self]) {
    }
    ch32h4_park_ack[self] = 0u;

    /* Cleared last: while the bit is set the interrupt stays pending, which is
     * harmless because we are inside it. */
    IPC->CLR = PARK_MASK(ch);

    if (prev & 8u) {
        __asm volatile("csrsi mstatus, 8");
    }
}

/* The attribute belongs on the declaration -- see ch32h4_irq.h. */
void CH32H4_IRQ_HANDLER(IPC_CH2_Handler_v3f);
void IPC_CH2_Handler_v3f(void) {
    park_spin(0u, PARK_CH_V3F);
}

void CH32H4_IRQ_HANDLER(IPC_CH3_Handler_v3f);
void IPC_CH3_Handler_v3f(void) {
    park_spin(1u, PARK_CH_V5F);
}

/* The V5F's table names these without the suffix. */
void CH32H4_IRQ_HANDLER(IPC_CH2_Handler);
void IPC_CH2_Handler(void) {
    park_spin(0u, PARK_CH_V3F);
}

void CH32H4_IRQ_HANDLER(IPC_CH3_Handler);
void IPC_CH3_Handler(void) {
    park_spin(1u, PARK_CH_V5F);
}

void ch32h4_park_live_here(void) {
    const uint8_t self = ch32h4_core_num() & 1u;
    const uint32_t ch = (self == 0u) ? PARK_CH_V3F : PARK_CH_V5F;

    ch32h4_park_req[self] = 0u;
    ch32h4_park_ack[self] = 0u;

    /* Receive interrupt on this core's channel. The bit layout is the one
     * IPC_Init() writes: RxIER is bit 5 of the channel's byte. Poked directly
     * rather than through the SDK so that nothing here depends on a flash
     * read at an awkward moment. */
    IPC->CTLR |= (1u << ((ch * 8u) + 5u));
    IPC->ENA |= PARK_MASK(ch);
    IPC->CLR = PARK_MASK(ch);

    NVIC_EnableIRQ((self == 0u) ? IPC_CH2_IRQn : IPC_CH3_IRQn);

    /* Last, so nothing asks this core to park before it can answer. */
    ch32h4_park_live[self] = 1u;
}

/* Kept for the V3F main loop and yield(): harmless belt and braces if the
 * interrupt is ever masked, and free when nobody is asking. */
void ch32h4_park_check(void) {
    const uint8_t self = ch32h4_core_num() & 1u;
    if (ch32h4_park_req[self] && !ch32h4_park_ack[self]) {
        park_spin(self, (self == 0u) ? PARK_CH_V3F : PARK_CH_V5F);
    }
}

bool ch32h4_park_other(uint32_t timeout_ms) {
    const uint8_t other = (ch32h4_core_num() & 1u) ^ 1u;

    /* Not running from flash, so there is nothing to protect it from. */
    if (!ch32h4_park_live[other]) {
        return true;
    }

    ch32h4_park_ack[other] = 0u;
    ch32h4_park_req[other] = 1u;
    IPC->SET = PARK_MASK((other == 0u) ? PARK_CH_V3F : PARK_CH_V5F);

    /* millis() rather than a spin count: this runs before the flash goes
     * busy, so this core's tick is still advancing. */
    const uint32_t start = millis();
    while (!ch32h4_park_ack[other]) {
        if ((uint32_t)(millis() - start) > timeout_ms) {
            ch32h4_park_req[other] = 0u;
            return false;
        }
    }
    return true;
}

void ch32h4_unpark_other(void) {
    const uint8_t other = (ch32h4_core_num() & 1u) ^ 1u;
    ch32h4_park_req[other] = 0u;
}
