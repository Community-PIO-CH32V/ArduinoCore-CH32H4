/* The fault record, in shared RAM so it survives the lockup reset.
 *
 * A lockup -- a fault inside the fault handler -- resets the part before the
 * handler can print, so "TRAP" never reaches the wire. Saving the CSRs to the
 * shared region first, where the V3F reads them on the next boot, turns that
 * silent reset into a printable fault.
 */
#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define CH32H4_FAULT_LOG_MAGIC  0xF01DFA11u


typedef struct {
    uint32_t magic;
    uint32_t mcause;
    uint32_t mepc;
    uint32_t mtval;
    uint32_t mstatus;
    uint32_t sp;
    uint32_t irq_eth;
    uint32_t irq_systick;
    uint32_t irq_usbfs;
    uint32_t irq_usart1;
    uint32_t irq_max_nesting;
    /* Consecutive boots that ended in a fault. The V3F counts them and stops
     * waking the V5F once there have been too many; the V5F clears it once it
     * reaches its runtime-ready point. See CH32H4_FAULT_REBOOT_LIMIT. */
    uint32_t boot_faults;
} ch32h4_fault_log_t;

/* How many crash-reboots in a row before the V3F stops relaunching the V5F.
 *
 * A fault handler that resets is right -- one that spins wedges the WCH-Link
 * with a 0x55 protocol error and needs NRST held down through an erase to
 * clear. But a fault that reproduces on every boot then resets forever, and a
 * part resetting a few times a second is just as unreachable. After this many
 * the V3F prints the record and idles with the console alive, which leaves a
 * board that can be talked to and reflashed. */
#define CH32H4_FAULT_REBOOT_LIMIT  3

/* Written once the trace region has been initialised. .xcore is NOLOAD, so on
 * a cold power-up it holds whatever the SRAM came up with -- and printing that
 * gives a boot report full of eight-digit nonsense that looks exactly like a
 * real crash from the run before.
 *
 * The build's own timestamp is folded in, so a reflash invalidates the region.
 * That matters more than it sounds: .xcore survives a reflash, and a new build
 * whose variables sit at different offsets reads the previous image's bytes
 * through the new layout and finds a plain magic exactly where it expects it.
 * A record only means anything within the image that wrote it.
 *
 * Only main_v3f.c uses this, so every reference in a build sees one __TIME__.
 * Range-check anything read out of .xcore anyway -- this narrows the window,
 * it does not close it. */
#define CH32H4_TRACE_MAGIC                                      (0x54000000u                                                 ^ ((uint32_t)sizeof(ch32h4_fault_log_t) << 16)              ^ ((uint32_t)__TIME__[0] << 20)                             ^ ((uint32_t)__TIME__[3] << 12)                             ^ ((uint32_t)__TIME__[4] << 8)                              ^ ((uint32_t)__TIME__[6] << 4)                              ^ (uint32_t)__TIME__[7])
extern volatile uint32_t ch32h4_trace_magic;

/* Lives in .xcore (shared RAM, NOLOAD). ch32h4_xcore_init() clears only the
 * FIFO indices, not the whole region, so this record is intact on the next
 * boot. */
/* volatile, and not decoratively so. The last thing the V3F does before
 * halting is clear boot_faults, and nothing reads it again on this side of the
 * reset -- so without volatile GCC drops the store as dead and the board comes
 * back up still refusing to run. The record is read after a reset and written
 * by whichever core is dying; every access has to survive the optimiser. */
extern volatile ch32h4_fault_log_t ch32h4_fault_log;

/* The same record for the V3F. That core has no console driver of its own at
 * trap time -- it may be mid-line, or the V5F may own the UART -- so its
 * handler only stores and resets, and the next boot prints. */
extern volatile ch32h4_fault_log_t ch32h4_fault_log_v3f;

/* Interrupt trace counters, kept in .bss (zeroed each boot). The fault log
 * records them so the V3F can report which interrupt was firing and how deeply
 * they were nesting when the fault hit. */
extern volatile uint32_t ch32h4_irq_eth_count;
extern volatile uint32_t ch32h4_irq_systick_count;
/* Last Ethernet frame handed to lwIP: [31:16] EtherType, [15:8] IP proto,
 * [7:0] frame length. Survives the reset so the V3F can say which frame was
 * being processed when the core locked up. */
extern volatile uint32_t ch32h4_eth_last_frame;
/* Length of the last frame handed to the MAC for transmit, so a lockup in the
 * TX path can be told apart from one in the RX path. */
extern volatile uint32_t ch32h4_eth_last_tx;
/* Coarse progress marker through the ETH RX/TX path, so a lockup can be
 * localized: 1 RX frame, 2 TX entry, 3 TX queued, 4 RX descriptor handed
 * back, 5 ETH interrupt. */
extern volatile uint32_t ch32h4_eth_phase;
extern volatile uint32_t ch32h4_irq_usbfs_count;
extern volatile uint32_t ch32h4_irq_usart1_count;
extern volatile uint32_t ch32h4_irq_max_nesting;
extern volatile uint32_t ch32h4_irq_nesting;

void ch32h4_irq_enter(volatile uint32_t *counter);
void ch32h4_irq_exit(void);

#ifdef __cplusplus
}
#endif
