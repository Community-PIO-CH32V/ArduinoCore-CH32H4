#include "Arduino.h"
#include "ch32h4_irq.h"

void CH32H4Serial::begin(unsigned long baud, uint16_t config) {
    /* The early console owns this same peripheral until now, and the reset
     * below would cut off whatever byte is still in its shift register. */
    ch32h4_console_flush();

    ch32h4_clock_enable(CH32_BUS_HB2, RCC_HB2Periph_USART1);
    ch32h4_block_reset(CH32_BUS_HB2, RCC_HB2Periph_USART1);

    /* TX push-pull, RX also on the alternate function -- NOT a floating
     * input. The mux owns the pad's output enable, so a floating input leaves
     * the peripheral disconnected while every status flag still reads
     * correctly. */
    ch32h4_pin_af(GPIOA, 9,  GPIO_AF7, CH32H4_CFG_AF_PP_50);
    ch32h4_pin_af(GPIOA, 10, GPIO_AF7, CH32H4_CFG_AF_PP_50);

    USART_InitTypeDef u = {};
    /* USART_Init divides HCLK via RCC_GetClocksFreq, which is correct as
     * shipped. SystemCoreClock would be four times off on this core. */
    u.USART_BaudRate = baud;

    switch (config & 0x07) {
        case 0x02: u.USART_WordLength = USART_WordLength_9b; break;
        default:   u.USART_WordLength = USART_WordLength_8b; break;
    }
    u.USART_StopBits = (config & 0x08) ? USART_StopBits_2 : USART_StopBits_1;
    if (config & 0x20) {
        u.USART_Parity = (config & 0x10) ? USART_Parity_Odd : USART_Parity_Even;
    } else {
        u.USART_Parity = USART_Parity_No;
    }
    u.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    u.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;

    USART_Init(_usart, &u);
    USART_ITConfig(_usart, USART_IT_RXNE, ENABLE);
    NVIC_EnableIRQ(USART1_IRQn);
    USART_Cmd(_usart, ENABLE);

    _head = _tail = 0;
    _running = true;
}

void CH32H4Serial::end() {
    flush();
    USART_ITConfig(_usart, USART_IT_RXNE, DISABLE);
    USART_Cmd(_usart, DISABLE);
    _running = false;
}

size_t CH32H4Serial::write(uint8_t c) {
    if (!_running) {
        return 0;
    }
    while (USART_GetFlagStatus(_usart, USART_FLAG_TXE) == RESET) {
    }
    USART_SendData(_usart, c);
    return 1;
}

int CH32H4Serial::available() {
    return (int)((uint16_t)(_head - _tail) & (RX_SIZE - 1));
}

int CH32H4Serial::peek() {
    if (_head == _tail) {
        return -1;
    }
    return _rx[_tail];
}

int CH32H4Serial::read() {
    if (_head == _tail) {
        return -1;
    }
    uint8_t c = _rx[_tail];
    _tail = (uint16_t)((_tail + 1) & (RX_SIZE - 1));
    return c;
}

void CH32H4Serial::flush() {
    if (!_running) {
        return;
    }
    while (USART_GetFlagStatus(_usart, USART_FLAG_TC) == RESET) {
    }
}

void CH32H4Serial::_isr() {
    if (USART_GetITStatus(_usart, USART_IT_RXNE) != RESET) {
        uint8_t c = (uint8_t)USART_ReceiveData(_usart);
        uint16_t next = (uint16_t)((_head + 1) & (RX_SIZE - 1));
        if (next != _tail) {          /* drop on overrun rather than wrap */
            _rx[_head] = c;
            _head = next;
        }
    }
    /* Clear ORE if it latched, or the peripheral stops delivering RXNE. */
    if (USART_GetFlagStatus(_usart, USART_FLAG_ORE) != RESET) {
        (void)USART_ReceiveData(_usart);
    }
}

CH32H4Serial Serial1(USART1);

/* The attribute belongs on the declaration -- see ch32h4_irq.h. */
extern "C" void CH32H4_IRQ_HANDLER(USART1_IRQHandler);
extern "C" void USART1_IRQHandler(void) {
    Serial1._isr();
}
