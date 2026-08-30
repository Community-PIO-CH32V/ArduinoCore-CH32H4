/* The two hooks lwipopts.h asks the platform for.
 *
 * Small, but both matter. sys_now drives every lwIP timer -- DHCP renewal, TCP
 * retransmission, ARP ageing -- and the core-locked assert is the only thing
 * standing between a dual-core sketch and a corrupted pbuf chain.
 */
#ifdef CH32H4_ETHERNET

#include "Arduino.h"
#include "ch32h4_xcore.h"

unsigned long ch32h4_lwip_now_ms(void) {
    return millis();
}

/* lwIP under NO_SYS has no locking of its own: it assumes exactly one context
 * calls into it. Here that context is the V5F -- the Ethernet interrupt and
 * the sketch's loop both run there.
 *
 * loop1() on the V3F calling into lwIP would race the receive path with
 * nothing to stop it, and the symptom would be a corrupted pbuf found long
 * afterwards. This turns that into a message at the moment it happens.
 *
 * It deliberately does not check for interrupt context: the Ethernet ISR is
 * allowed in, because that is how the receive path works. */
void ch32h4_lwip_assert_core_locked(void) {
    if (ch32h4_core_num() != 1u) {
        ch32h4_console_puts(
            "\nlwIP called from the V3F. The stack belongs to the V5F: "
            "call it from setup()/loop(), not setup1()/loop1().\n");
        ch32h4_console_flush();
        for (;;) {
        }
    }
}

#endif /* CH32H4_ETHERNET */
