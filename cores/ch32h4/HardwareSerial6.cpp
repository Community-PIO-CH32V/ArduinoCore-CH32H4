/* Serial6, on USART6.
 *
 * Its own file so that a sketch which never mentions Serial6 does not link it.
 * The object, its 256-byte receive buffer, its constructor and the vector
 * below all come in together or not at all -- which only works because the
 * core archive is no longer linked with --whole-archive.
 *
 * Default pins: PA0 TX, PA1 RX -- also A0/A1, so an analogRead on those ends here.
 * Move them with Serial6.setTX()/setRX() before begin(); the variant's
 * g_uart_tx_map lists every pin this USART can use.
 */
#include "Arduino.h"
#include "ch32h4_irq.h"

CH32H4Serial Serial6(6, PA0, PA1);

/* The attribute belongs on the declaration -- see ch32h4_irq.h. */
extern "C" void CH32H4_IRQ_HANDLER(USART6_IRQHandler);
extern "C" void USART6_IRQHandler(void) {
    Serial6._isr();
}
