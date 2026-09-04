/* Serial7, on USART7.
 *
 * Its own file so that a sketch which never mentions Serial7 does not link it.
 * The object, its 256-byte receive buffer, its constructor and the vector
 * below all come in together or not at all -- which only works because the
 * core archive is no longer linked with --whole-archive.
 *
 * Default pins: PB6 TX, PB5 RX -- PB6 is also Wire's default SCL.
 * Move them with Serial7.setTX()/setRX() before begin(); the variant's
 * g_uart_tx_map lists every pin this USART can use.
 */
#include "Arduino.h"
#include "ch32h4_irq.h"

CH32H4Serial Serial7(7, PB6, PB5);

/* The attribute belongs on the declaration -- see ch32h4_irq.h. */
extern "C" void CH32H4_IRQ_HANDLER(USART7_IRQHandler);
extern "C" void USART7_IRQHandler(void) {
    Serial7._isr();
}
