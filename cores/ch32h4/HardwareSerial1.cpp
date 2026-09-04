/* Serial1, on USART1.
 *
 * Its own file so that a sketch which never mentions Serial1 does not link it.
 * The object, its 256-byte receive buffer, its constructor and the vector
 * below all come in together or not at all -- which only works because the
 * core archive is no longer linked with --whole-archive.
 *
 * Default pins: PA9 TX, PA10 RX -- the WCH-Link VCP.
 * Move them with Serial1.setTX()/setRX() before begin(); the variant's
 * g_uart_tx_map lists every pin this USART can use.
 */
#include "Arduino.h"
#include "ch32h4_irq.h"
#include "ch32h4_fault.h"

CH32H4Serial Serial1(1, PA9, PA10);

/* The attribute belongs on the declaration -- see ch32h4_irq.h. */
extern "C" void CH32H4_IRQ_HANDLER(USART1_IRQHandler);
extern "C" void USART1_IRQHandler(void) {
    ch32h4_irq_enter(&ch32h4_irq_usart1_count);
    Serial1._isr();
    ch32h4_irq_exit();
}
