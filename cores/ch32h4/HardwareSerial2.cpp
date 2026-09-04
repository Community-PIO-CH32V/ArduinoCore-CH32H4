/* Serial2, on USART2.
 *
 * Its own file so that a sketch which never mentions Serial2 does not link it.
 * The object, its 256-byte receive buffer, its constructor and the vector
 * below all come in together or not at all -- which only works because the
 * core archive is no longer linked with --whole-archive.
 *
 * Default pins: PA2 TX, PA3 RX -- free on this package.
 * Move them with Serial2.setTX()/setRX() before begin(); the variant's
 * g_uart_tx_map lists every pin this USART can use.
 */
#include "Arduino.h"
#include "ch32h4_irq.h"

CH32H4Serial Serial2(2, PA2, PA3);

/* The attribute belongs on the declaration -- see ch32h4_irq.h. */
extern "C" void CH32H4_IRQ_HANDLER(USART2_IRQHandler);
extern "C" void USART2_IRQHandler(void) {
    Serial2._isr();
}
