#include "ch32h4_console.h"
#include "ch32h4_xcore.h"
#include "ch32h417.h"
#include <stdbool.h>

/* Recursion depth, per core. HSEM records the taking core as the owner and
 * refuses a second take from it, so re-entrancy is counted here rather than
 * relying on the semaphore to be recursive. It lives in .xcore because .bss is
 * shared between the two cores and this must not be. */
static volatile uint32_t s_lock_depth[2] CH32H4_XCORE;

void ch32h4_console_init(uint32_t baud) {
    /* The depth counters live in .xcore, which is NOLOAD -- on a cold boot
     * they hold whatever the SRAM came up with. A non-zero start means
     * ch32h4_console_lock() believes it already holds the semaphore and never
     * takes it, so the lock silently does nothing; a core that then "releases"
     * from a depth it never reached holds it forever. Only the V3F calls this,
     * and only before the V5F is awake, so clearing both here is safe. */
    s_lock_depth[0] = 0;
    s_lock_depth[1] = 0;

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

void ch32h4_console_lock(void) {
    const uint8_t core = ch32h4_core_num() & 1u;
    if (s_lock_depth[core]++ != 0u) {
        return;
    }
    /* Bounded -- roughly a second at 100 MHz. Giving up and printing anyway is
     * the right failure: interleaved output beats no output when the other
     * core has died holding the semaphore, and this driver exists precisely
     * for the moments when things are broken. */
    uint32_t guard = 100000000u;
    while (!ch32h4_mutex_try_lock(CH32H4_HSEM_CONSOLE) && --guard) {
    }
}

void ch32h4_console_unlock(void) {
    const uint8_t core = ch32h4_core_num() & 1u;
    if (s_lock_depth[core] == 0u) {
        return;
    }
    if (--s_lock_depth[core] == 0u) {
        ch32h4_mutex_unlock(CH32H4_HSEM_CONSOLE);
    }
}

void ch32h4_console_putc(char c) {
    while (USART_GetFlagStatus(USART1, USART_FLAG_TXE) == RESET) {
    }
    USART_SendData(USART1, (uint16_t)(uint8_t)c);
}

void ch32h4_console_puts(const char *s) {
    ch32h4_console_lock();
    for (; *s; s++) {
        if (*s == '\n') {
            ch32h4_console_putc('\r');
        }
        ch32h4_console_putc(*s);
    }
    ch32h4_console_unlock();
}

void ch32h4_console_flush(void) {
    /* TC, not TXE: TXE only says the holding register is free. */
    uint32_t guard = 2000000u;
    while (USART_GetFlagStatus(USART1, USART_FLAG_TC) == RESET && --guard) {
    }
}

void ch32h4_console_putu(uint32_t v) {
    char buf[11];
    int i = 0;
    ch32h4_console_lock();
    if (v == 0) {
        ch32h4_console_putc('0');
        ch32h4_console_unlock();
        return;
    }
    while (v) {
        buf[i++] = (char)('0' + (v % 10u));
        v /= 10u;
    }
    while (i--) {
        ch32h4_console_putc(buf[i]);
    }
    ch32h4_console_unlock();
}

void ch32h4_console_puthex(uint32_t v) {
    static const char digits[] = "0123456789abcdef";
    ch32h4_console_lock();
    ch32h4_console_puts("0x");
    for (int i = 28; i >= 0; i -= 4) {
        ch32h4_console_putc(digits[(v >> i) & 0xFu]);
    }
    ch32h4_console_unlock();
}
