/* Serial5, on USART5.
 *
 * Its own file so that a sketch which never mentions Serial5 does not link it.
 * The object, its 256-byte receive buffer, its constructor and the vector
 * below all come in together or not at all -- which only works because the
 * core archive is no longer linked with --whole-archive.
 *
 * Default pins: PE0 TX, PE2 RX -- PE2 is on the 3.3 V rail, PE0 is not.
 * Move them with Serial5.setTX()/setRX() before begin(); the variant's
 * g_uart_tx_map lists every pin this USART can use.
 */
#include "Arduino.h"
#include "ch32h4_irq.h"

CH32H4Serial Serial5(5, PE0, PE2);

/* The attribute belongs on the declaration -- see ch32h4_irq.h. */
extern "C" void CH32H4_IRQ_HANDLER(USART5_IRQHandler);
extern "C" void USART5_IRQHandler(void) {
    Serial5._isr();
}
