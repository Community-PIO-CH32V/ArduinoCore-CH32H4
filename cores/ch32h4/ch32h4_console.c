#include "ch32h4_console.h"
#include "ch32h417.h"

void ch32h4_console_init(uint32_t baud) {
    GPIO_InitTypeDef gpio = {0};
    USART_InitTypeDef usart = {0};

    /* USART1, GPIOA and AFIO are all on HB2. There is no APB on this family,
     * and enabling a bit on the wrong bus produces no error at all -- the
     * registers simply read back as zeroes and the writes are discarded. */
    RCC_HB2PeriphClockCmd(RCC_HB2Periph_GPIOA | RCC_HB2Periph_USART1
                          | RCC_HB2Periph_AFIO, ENABLE);

    /* RCC_*PeriphClockCmd is a read-modify-write with no read-back, and an
     * access immediately after enabling a clock can be dropped. */
    (void)RCC->HB2PCENR;

    /* GPIO here is two mechanisms and both are required: an F1-style mode
     * register (CFGLR/CFGHR), and an F4-style alternate-function mux. Setting
     * AF_PP without GPIO_PinAFConfig gives a peripheral that runs, whose every
     * status flag is correct, and nothing on the wire. The mux write is itself
     * discarded if AFIO's clock is off, which is why it is enabled above. */
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource9,  GPIO_AF7);
    GPIO_PinAFConfig(GPIOA, GPIO_PinSource10, GPIO_AF7);

    gpio.GPIO_Pin   = GPIO_Pin_9;
    gpio.GPIO_Mode  = GPIO_Mode_AF_PP;
    gpio.GPIO_Speed = GPIO_Speed_High;
    GPIO_Init(GPIOA, &gpio);

    /* RX must be AF too, not a floating input: the mux owns the pad's output
     * enable, and a floating input leaves the peripheral disconnected. */
    gpio.GPIO_Pin  = GPIO_Pin_10;
    gpio.GPIO_Mode = GPIO_Mode_AF_PP;
    GPIO_Init(GPIOA, &gpio);

    /* USART_Init divides HCLK, which is correct as shipped. SystemCoreClock
     * would be four times off on the V5F and no test on the board could see
     * it, because everything downstream would agree with itself. */
    usart.USART_BaudRate = baud;
    usart.USART_WordLength = USART_WordLength_8b;
    usart.USART_StopBits = USART_StopBits_1;
    usart.USART_Parity = USART_Parity_No;
    usart.USART_HardwareFlowControl = USART_HardwareFlowControl_None;
    usart.USART_Mode = USART_Mode_Tx | USART_Mode_Rx;
    USART_Init(USART1, &usart);
    USART_Cmd(USART1, ENABLE);
}

void ch32h4_console_putc(char c) {
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) {
    }
    USART_SendData(USART1, (uint16_t)(uint8_t)c);
}

void ch32h4_console_puts(const char *s) {
    for (; *s; s++) {
        if (*s == '\n') {
            ch32h4_console_putc('\r');
        }
        ch32h4_console_putc(*s);
    }
}

void ch32h4_console_putu(uint32_t v) {
    char buf[11];
    int i = 0;
    if (v == 0) {
        ch32h4_console_putc('0');
        return;
    }
    while (v) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i--) {
        ch32h4_console_putc(buf[i]);
    }
}

void ch32h4_console_puthex(uint32_t v) {
    static const char digits[] = "0123456789abcdef";
    ch32h4_console_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        ch32h4_console_putc(digits[(v >> i) & 0xFu]);
    }
}
