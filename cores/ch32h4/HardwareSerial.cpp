#include "Arduino.h"
#include "ch32h4_fault.h"
#include "ch32h4_irq.h"

bool CH32H4Serial::setTX(pin_size_t pin) {
    if (_running || !ch32h4_uart_tx_af(_id, pin, nullptr)) {
        return false;
    }
    _tx = pin;
    return true;
}

bool CH32H4Serial::setRX(pin_size_t pin) {
    if (_running || !ch32h4_uart_rx_af(_id, pin, nullptr)) {
        return false;
    }
    _rx_pin = pin;
    return true;
}

void CH32H4Serial::begin(unsigned long baud, uint16_t config) {
    uint8_t tx_af = 0, rx_af = 0;
    if (!ch32h4_uart_tx_af(_id, _tx, &tx_af) ||
        !ch32h4_uart_rx_af(_id, _rx_pin, &rx_af)) {
        /* The pin cannot carry this signal for this peripheral. Starting
         * anyway would give a port whose flags all read correctly and whose
         * output never leaves the die. */
        return;
    }

    _usart = ch32h4_uart_dev(_id);
    if (_usart == nullptr) {
        return;
    }

    /* The early console owns USART1 until now, and the reset below would cut
     * off whatever byte is still in its shift register. No other port is
     * shared with it. */
    if (_id == 1) {
        ch32h4_console_flush();
    }

    ch32h4_uart_clock_enable(_id);
    ch32h4_uart_reset(_id);

    /* TX push-pull, RX also on the alternate function -- NOT a floating
     * input. The mux owns the pad's output enable, so a floating input leaves
     * the peripheral disconnected while every status flag still reads
     * correctly. */
    ch32h4_pin_af(g_pins[_tx].port, g_pins[_tx].bit, tx_af,
                  CH32H4_CFG_AF_PP_50);
    ch32h4_pin_af(g_pins[_rx_pin].port, g_pins[_rx_pin].bit, rx_af,
                  CH32H4_CFG_AF_PP_50);

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
    const int irqn = ch32h4_uart_irqn(_id);
    if (irqn >= 0) {
        NVIC_EnableIRQ((IRQn_Type)irqn);
    }
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
    /* The same semaphore ch32h4_console_puts() takes. Two drivers share this
     * peripheral and two cores share both drivers, and nothing in the USART
     * serialises them -- see the comment on CH32H4_HSEM_CONSOLE. */
    /* Only USART1 is shared with the boot console and the other core; the
     * rest belong to this sketch alone and paying for a hardware semaphore on
     * every byte would be a cost with nothing on the other side of it. */
    const bool shared = (_id == 1);
    if (shared) {
        ch32h4_console_lock();
    }
    while (USART_GetFlagStatus(_usart, USART_FLAG_TXE) == RESET) {
    }
    USART_SendData(_usart, c);
    if (shared) {
        ch32h4_console_unlock();
    }
    return 1;
}

size_t CH32H4Serial::write(const uint8_t *buf, size_t n) {
    if (!_running || buf == nullptr) {
        return 0;
    }
    /* Taken once for the whole buffer. The lock is recursive per core, so the
     * per-byte write() below nests without a second take. */
    const bool shared = (_id == 1);
    if (shared) {
        ch32h4_console_lock();
    }
    for (size_t i = 0; i < n; i++) {
        write(buf[i]);
    }
    if (shared) {
        ch32h4_console_unlock();
    }
    return n;
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
