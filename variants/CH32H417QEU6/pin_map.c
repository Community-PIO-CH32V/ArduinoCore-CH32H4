/* Which timer channels each pin can be, on this package.
 *
 * This is silicon, not board wiring, but it belongs to the variant because a
 * different package bonds out a different set of pads -- and because a board
 * may deliberately withhold a pin that the silicon would allow.
 *
 * A pin can appear more than once: most have a choice of timers, and
 * ch32h4_pwm_find() takes the first entry that is free, so the order here is
 * a preference. Non-negated options are listed before negated ones for the
 * same pin, because a plain output is almost always what a caller wants.
 *
 * `negated` marks TIMx_CHyN, the complementary output. It shares its compare
 * register with the matching TIMx_CHy, so the two cannot carry different
 * duties, and it idles inverted. analogWrite() will not select one on its own;
 * a caller has to ask for it explicitly through ch32h4_pwm_find_on_timer().
 *
 * Supply domains matter here as much as they do for I2C and SPI: only PA5-PA7
 * and PE2-PE6 are on the 3.3 V rail, and everything else sits on VIO18 and
 * idles well below 3.3 V. See PIN_IS_3V3_DOMAIN in pins_arduino.h.
 *
 * Table taken from the MicroPython port for this silicon, which derived it
 * from the reference manual's alternate-function tables and exercised it on
 * this board.
 */
#include "Arduino.h"
#include "ch32h4_pinmap.h"

const ch32h4_pwm_af_t g_pwm_af_map[] = {

    { PA0  ,  2, 1, false,  1 },
    { PA0  ,  5, 1, false,  2 },
    { PA0  ,  9, 1, false,  6 },
    { PA1  ,  2, 2, false,  1 },
    { PA1  ,  5, 2, false,  2 },
    { PA1  ,  9, 2, false,  6 },
    { PA2  ,  2, 3, false,  1 },
    { PA2  ,  5, 3, false,  2 },
    { PA2  ,  9, 3, false,  4 },
    { PA3  ,  2, 4, false,  1 },
    { PA3  ,  5, 4, false,  2 },
    { PA3  ,  9, 4, false,  4 },
    { PA3  , 10, 3, false,  8 },
    { PA4  , 10, 4, false,  9 },
    { PA5  ,  2, 1, false,  1 },
    { PA5  ,  8, 1, true ,  3 },
    { PA6  ,  3, 1, false,  2 },
    { PA6  , 10, 1, false,  9 },
    { PA7  ,  3, 2, false,  2 },
    { PA7  , 10, 2, false,  9 },
    { PA7  ,  1, 1, true ,  1 },
    { PA7  ,  8, 1, true ,  3 },
    { PA8  ,  1, 1, false,  1 },
    { PA9  ,  1, 2, false,  1 },   /* also USART1 TX, the REPL */
    { PA10 ,  1, 3, false,  1 },   /* also USART1 RX, the REPL */
    { PA11 ,  1, 4, false,  1 },   /* also USB D- */
    { PA15 ,  2, 1, false,  1 },

    { PB0  ,  3, 3, false,  2 },
    { PB0  ,  5, 4, false,  4 },
    { PB0  ,  1, 2, true ,  1 },
    { PB0  ,  8, 2, true ,  3 },
    { PB1  ,  3, 4, false,  2 },
    { PB1  , 12, 1, false,  5 },
    { PB1  ,  1, 3, true ,  1 },
    { PB1  ,  8, 3, true ,  3 },
    { PB2  , 12, 2, false,  5 },
    { PB3  ,  2, 2, false,  1 },
    { PB4  ,  3, 1, false,  2 },
    { PB5  ,  3, 2, false,  2 },
    { PB6  ,  4, 1, false,  2 },
    { PB6  , 10, 1, false,  0 },
    { PB7  ,  4, 2, false,  2 },
    { PB7  , 10, 2, false,  0 },
    { PB8  ,  4, 3, false,  2 },
    { PB8  , 10, 3, false,  1 },
    { PB9  ,  4, 4, false,  2 },
    { PB9  , 10, 4, false,  1 },
    { PB10 ,  2, 3, false,  1 },
    { PB10 ,  9, 2, false,  2 },
    { PB11 ,  2, 4, false,  1 },
    { PB11 ,  9, 4, false,  9 },
    { PB12 ,  9, 3, false,  8 },
    { PB13 ,  1, 1, true ,  1 },
    { PB14 ,  9, 1, false,  2 },
    { PB14 ,  1, 2, true ,  1 },
    { PB14 ,  8, 2, true ,  3 },
    { PB15 ,  9, 2, false,  2 },
    { PB15 ,  1, 3, true ,  1 },
    { PB15 ,  8, 3, true ,  3 },

    { PC1  ,  5, 1, false,  2 },
    { PC1  ,  8, 1, true ,  0 },
    { PC2  ,  5, 2, false,  2 },
    { PC2  ,  8, 2, true ,  0 },
    { PC3  ,  5, 3, false,  2 },
    { PC3  ,  8, 3, true ,  0 },
    { PC6  ,  3, 1, false,  2 },
    { PC6  ,  8, 1, false,  3 },
    { PC7  ,  3, 2, false,  2 },
    { PC7  ,  8, 2, false,  3 },
    { PC8  ,  3, 3, false,  2 },
    { PC8  ,  8, 3, false,  3 },
    { PC9  ,  3, 4, false,  2 },
    { PC9  ,  8, 4, false,  3 },
    { PC9  ,  9, 1, false,  6 },
    { PC11 ,  9, 4, false,  2 },
    { PC12 ,  9, 3, false,  2 },

    { PD3  , 11, 1, false,  2 },
    { PD4  ,  3, 2, false,  9 },
    { PD4  , 11, 2, false,  2 },
    { PD5  ,  3, 3, false,  9 },
    { PD5  , 11, 3, false,  2 },
    { PD6  ,  3, 4, false,  9 },
    { PD6  , 11, 4, false,  2 },
    { PD7  , 11, 3, false, 13 },
    { PD12 ,  4, 1, false,  2 },
    { PD12 ,  5, 1, false,  6 },
    { PD13 ,  4, 2, false,  2 },
    { PD13 ,  5, 2, false,  6 },
    { PD14 ,  4, 3, false,  2 },
    { PD14 ,  5, 3, false,  6 },
    { PD15 ,  4, 4, false,  2 },
    { PD15 ,  5, 4, false,  6 },

    { PE0  , 11, 1, false, 13 },
    { PE1  , 11, 2, false, 13 },
    { PE3  ,  4, 1, false,  2 },
    { PE3  ,  8, 1, false,  0 },
    { PE3  , 12, 1, false,  3 },
    { PE4  ,  4, 2, false,  2 },
    { PE4  ,  8, 2, false,  0 },
    { PE4  , 12, 2, false,  3 },
    { PE5  ,  4, 3, false,  2 },
    { PE5  ,  8, 3, false,  0 },
    { PE5  ,  9, 3, false,  4 },
    { PE5  , 12, 3, false,  3 },
    { PE6  ,  4, 4, false,  2 },
    { PE6  ,  8, 4, false,  0 },
    { PE6  ,  9, 4, false,  4 },
    { PE6  , 12, 4, false,  3 },
    { PE8  ,  1, 1, true ,  1 },
    { PE9  ,  1, 1, false,  1 },
    { PE10 ,  1, 2, true ,  1 },
    { PE11 ,  1, 2, false,  1 },
    { PE12 ,  1, 3, true ,  1 },
    { PE13 ,  1, 3, false,  1 },
    { PE13 , 12, 2, false,  2 },
    { PE14 ,  1, 4, false,  1 },
    { PE14 , 12, 3, false,  2 },
    { PE15 , 12, 4, false,  2 },

    { PF6  , 10, 3, false,  9 },
    { PF6  , 11, 1, false, 13 },
    { PF7  , 10, 4, false,  9 },
    { PF7  , 11, 2, false, 13 },
    { PF8  , 10, 1, false,  9 },
    { PF8  , 11, 3, false, 13 },
    { PF9  , 10, 2, false,  9 },
    { PF12 , 12, 3, false, 13 },
    { PF13 , 12, 4, false, 13 },
};

const size_t g_pwm_af_map_len = sizeof(g_pwm_af_map) / sizeof(g_pwm_af_map[0]);

/* ---- SPI ----------------------------------------------------------------
 *
 * Entries that collide with something this board already uses are listed last
 * for their peripheral, so a search that takes the first match never picks
 * them: SPI2_SCK on PA9 is the console's UART TX, and on PA12 it is USB D+.
 *
 * Only PA5-PA7 and PE2-PE6 are on the 3.3 V rail. Everything else sits on
 * VIO18 and idles well below 3.3 V, which a 3.3 V peripheral may not read as a
 * high. See PIN_IS_3V3_DOMAIN in pins_arduino.h.
 */
const ch32h4_periph_pin_t g_spi_sck_map[] = {
    { 1, PA5,  5 },   /* 3.3 V domain */
    { 1, PB3,  5 },
    { 1, PF5,  5 },
    { 1, PF7,  3 },
    { 2, PB13, 5 },
    { 2, PB10, 5 },
    { 2, PC2,  5 },
    { 2, PD3,  5 },
    { 2, PE14, 5 },
    { 2, PA9,  5 },   /* also USART1 TX -- the console */
    { 2, PA12, 5 },   /* also USB D+ */
    { 3, PB3,  6 },
    { 3, PC10, 6 },
    { 3, PA14, 1 },
    { 4, PE2,  5 },   /* 3.3 V domain */
    { 4, PE12, 5 },
};
const size_t g_spi_sck_map_len = sizeof(g_spi_sck_map) / sizeof(g_spi_sck_map[0]);

const ch32h4_periph_pin_t g_spi_miso_map[] = {
    { 1, PA6,  5 },   /* 3.3 V domain */
    { 1, PB4,  5 },
    { 1, PF3,  5 },
    { 1, PF9,  3 },
    { 2, PB14, 5 },
    { 2, PC2,  5 },
    { 3, PC11, 6 },
    { 3, PB4,  6 },
    { 3, PC9,  5 },
    { 4, PE5,  5 },   /* 3.3 V domain */
    { 4, PE13, 5 },
};
const size_t g_spi_miso_map_len = sizeof(g_spi_miso_map) / sizeof(g_spi_miso_map[0]);

const ch32h4_periph_pin_t g_spi_mosi_map[] = {
    { 1, PA7,  5 },   /* 3.3 V domain */
    { 1, PB5,  5 },
    { 1, PD7,  5 },
    { 1, PF8,  3 },
    { 2, PB15, 5 },
    { 2, PC3,  5 },
    { 2, PC1,  5 },
    { 3, PC12, 6 },
    { 3, PB5,  7 },
    { 3, PB2,  7 },
    { 3, PD6,  5 },
    { 3, PA13, 1 },
    { 4, PE6,  5 },   /* 3.3 V domain */
    { 4, PE14, 5 },
};
const size_t g_spi_mosi_map_len = sizeof(g_spi_mosi_map) / sizeof(g_spi_mosi_map[0]);

/* ---- I2C ----------------------------------------------------------------
 *
 * SCL and SDA are paired because the silicon pairs them.
 *
 * Note that I2C pins are open-drain and this part has NO internal pull-up in
 * that mode -- the F1-style encoding does not offer one -- so every one of
 * these needs real resistors. A driver should report a missing resistor as a
 * missing resistor, not as a missing device.
 */
const ch32h4_i2c_pin_t g_i2c_map[] = {
    { 1, PB6,  PB7,  4 },   /* 3.3 V domain; the board's SSD1306 is here */
    { 1, PB8,  PB9,  4 },   /* also SWCLK/SWDIO -- taking these loses debug */
    { 2, PB10, PB11, 4 },
    { 2, PC0,  PC1,  9 },
    { 3, PA8,  PC9,  4 },
    { 3, PA14, PA13, 7 },
    { 4, PD12, PD13, 4 },
    { 4, PF12, PF13, 2 },
};
const size_t g_i2c_map_len = sizeof(g_i2c_map) / sizeof(g_i2c_map[0]);

/* Which pins can carry USART TX, and with which alternate
 * function. Same provenance as the timer table above: the
 * MicroPython port for this silicon generated it from the
 * datasheet's per-pin function lists.
 *
 * A WRONG AF NUMBER FAILS SILENTLY. The USART runs, STATR shows TXE
 * and TC set, BRR holds the right divisor, and nothing reaches the
 * wire -- which is why this is a table and not eight #defines
 * somebody typed.
 *
 * This part multiplexes like an STM32F4 rather than remapping like a
 * CH32V307, so moving a USART to other pins means choosing another
 * row here, not flipping a remap bit. */
const ch32h4_periph_pin_t g_uart_tx_map[] = {
    /* USART1 */
    { 1, PA9  ,  7 },
    { 1, PB6  ,  7 },
    { 1, PB14 ,  4 },
    { 1, PD13 , 14 },
    /* USART2 */
    { 2, PA2  ,  7 },
    { 2, PD5  ,  7 },
    /* USART3 */
    { 3, PA13 ,  4 },
    { 3, PB10 ,  7 },
    { 3, PC10 ,  7 },
    { 3, PE14 ,  7 },
    /* USART4 */
    { 4, PC6  ,  7 },
    { 4, PF4  ,  7 },
    /* USART5 */
    { 5, PE0  ,  4 },
    { 5, PE3  , 11 },
    /* USART6 */
    { 6, PA0  ,  8 },
    { 6, PA12 ,  6 },
    { 6, PB9  ,  8 },
    { 6, PC10 ,  8 },
    { 6, PD1  ,  8 },
    /* USART7 */
    { 7, PB6  , 14 },
    { 7, PB13 , 14 },
    { 7, PC12 ,  8 },
    /* USART8 */
    { 8, PA15 , 11 },
    { 8, PB4  , 11 },
    { 8, PE8  ,  7 },
    { 8, PF7  ,  7 },
};
const size_t g_uart_tx_map_len = sizeof(g_uart_tx_map) / sizeof(g_uart_tx_map[0]);

/* Which pins can carry USART RX, and with which alternate
 * function. Same provenance as the timer table above: the
 * MicroPython port for this silicon generated it from the
 * datasheet's per-pin function lists.
 *
 * A WRONG AF NUMBER FAILS SILENTLY. The USART runs, STATR shows TXE
 * and TC set, BRR holds the right divisor, and nothing reaches the
 * wire -- which is why this is a table and not eight #defines
 * somebody typed.
 *
 * This part multiplexes like an STM32F4 rather than remapping like a
 * CH32V307, so moving a USART to other pins means choosing another
 * row here, not flipping a remap bit. */
const ch32h4_periph_pin_t g_uart_rx_map[] = {
    /* USART1 */
    { 1, PA10 ,  7 },
    { 1, PB7  ,  7 },
    { 1, PB15 ,  4 },
    { 1, PD12 , 14 },
    /* USART2 */
    { 2, PA3  ,  7 },
    { 2, PD6  ,  7 },
    /* USART3 */
    { 3, PA14 ,  4 },
    { 3, PB11 ,  7 },
    { 3, PC11 ,  7 },
    { 3, PD9  ,  7 },
    /* USART4 */
    { 4, PC7  ,  7 },
    { 4, PF3  ,  7 },
    /* USART5 */
    { 5, PE2  ,  4 },
    { 5, PF5  ,  4 },
    /* USART6 */
    { 6, PA1  ,  8 },
    { 6, PA11 ,  6 },
    { 6, PB8  ,  8 },
    { 6, PC11 ,  8 },
    { 6, PD0  ,  8 },
    /* USART7 */
    { 7, PB5  , 14 },
    { 7, PB12 , 14 },
    { 7, PD2  ,  8 },
    /* USART8 */
    { 8, PA8  , 11 },
    { 8, PB3  , 11 },
    { 8, PE7  ,  7 },
    { 8, PF6  ,  7 },
};
const size_t g_uart_rx_map_len = sizeof(g_uart_rx_map) / sizeof(g_uart_rx_map[0]);
