/* Serial3, on USART3.
 *
 * Its own file so that a sketch which never mentions Serial3 does not link it.
 * The object, its 256-byte receive buffer, its constructor and the vector
 * below all come in together or not at all -- which only works because the
 * core archive is no longer linked with --whole-archive.
 *
 * Default pins: PB10 TX, PB11 RX -- free on this package.
 * Move them with Serial3.setTX()/setRX() before begin(); the variant's
 * g_uart_tx_map lists every pin this USART can use.
 */
#include "Arduino.h"
#include "ch32h4_irq.h"

CH32H4Serial Serial3(3, PB10, PB11);

/* The attribute belongs on the declaration -- see ch32h4_irq.h. */
extern "C" void CH32H4_IRQ_HANDLER(USART3_IRQHandler);
extern "C" void USART3_IRQHandler(void) {
    Serial3._isr();
}
