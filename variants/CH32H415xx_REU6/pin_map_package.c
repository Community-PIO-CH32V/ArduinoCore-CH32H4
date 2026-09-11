/* Peripheral pin maps for the CH32H415REU6. GENERATED -- see tools/genvariants.py.
 *
 * Derived from the CH32H417QEU6 maps, which came from the MicroPython port for
 * this silicon and have run on hardware, by keeping only the entries this part
 * can actually perform: the pins must be bonded on the QFN60 package, and
 * this part's own datasheet function list must confirm the signal at that
 * alternate-function number.
 *
 * An entry the part's table does not confirm is DROPPED rather than adjusted.
 * The table can therefore be incomplete, but it cannot point a peripheral at
 * the wrong pad, which is the failure that would be invisible.
 *
 * Entries kept, by table:
 *   g_i2c_map          5 kept,   3 dropped
 *   g_pwm_af_map      95 kept,  32 dropped
 *   g_spi_miso_map     9 kept,   2 dropped
 *   g_spi_mosi_map     8 kept,   6 dropped
 *   g_spi_nss_map      1 kept,   0 dropped
 *   g_spi_sck_map     12 kept,   4 dropped
 *   g_uart_rx_map     15 kept,  11 dropped
 *   g_uart_tx_map     17 kept,   9 dropped */
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
    { PA9  ,  1, 2, false,  1 },
    { PA10 ,  1, 3, false,  1 },
    { PA11 ,  1, 4, false,  1 },
    { PA15 ,  2, 1, false,  1 },
    { PB0  ,  3, 3, false,  2 },
    { PB0  ,  5, 4, false,  4 },
    { PB0  ,  1, 2, true ,  1 },
    { PB0  ,  8, 2, true ,  3 },
    { PB1  ,  3, 4, false,  2 },
    { PB1  , 12, 1, false,  5 },
    { PB1  ,  1, 3, true ,  1 },
    { PB1  ,  8, 3, true ,  3 },
    { PB3  ,  2, 2, false,  1 },
    { PB4  ,  3, 1, false,  2 },
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
    { PE0  , 11, 1, false, 13 },
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
    { PE11 ,  1, 2, false,  1 },
    { PE12 ,  1, 3, true ,  1 },
    { PE13 ,  1, 3, false,  1 },
    { PE13 , 12, 2, false,  2 },
    { PE14 ,  1, 4, false,  1 },
    { PE14 , 12, 3, false,  2 },
};
const size_t g_pwm_af_map_len = sizeof(g_pwm_af_map) / sizeof(g_pwm_af_map[0]);

const ch32h4_periph_pin_t g_spi_sck_map[] = {
    { 1, PA5  ,  5 },
    { 1, PB3  ,  5 },
    { 1, PF5  ,  5 },
    { 2, PB13 ,  5 },
    { 2, PB10 ,  5 },
    { 2, PD3  ,  5 },
    { 2, PA9  ,  5 },
    { 2, PA12 ,  5 },
    { 3, PB3  ,  6 },
    { 3, PC10 ,  6 },
    { 3, PA14 ,  1 },
    { 4, PE12 ,  5 },
};
const size_t g_spi_sck_map_len = sizeof(g_spi_sck_map) / sizeof(g_spi_sck_map[0]);

const ch32h4_periph_pin_t g_spi_miso_map[] = {
    { 1, PA6  ,  5 },
    { 1, PB4  ,  5 },
    { 1, PF3  ,  5 },
    { 2, PC2  ,  5 },
    { 3, PC11 ,  6 },
    { 3, PB4  ,  6 },
    { 3, PC9  ,  5 },
    { 4, PE5  ,  5 },
    { 4, PE13 ,  5 },
};
const size_t g_spi_miso_map_len = sizeof(g_spi_miso_map) / sizeof(g_spi_miso_map[0]);

const ch32h4_periph_pin_t g_spi_mosi_map[] = {
    { 1, PA7  ,  5 },
    { 2, PB15 ,  5 },
    { 2, PC3  ,  5 },
    { 2, PC1  ,  5 },
    { 3, PC12 ,  6 },
    { 3, PA13 ,  1 },
    { 4, PE6  ,  5 },
    { 4, PE14 ,  5 },
};
const size_t g_spi_mosi_map_len = sizeof(g_spi_mosi_map) / sizeof(g_spi_mosi_map[0]);

const ch32h4_periph_pin_t g_spi_nss_map[] = {
    { 4, PE4  ,  5 },
};
const size_t g_spi_nss_map_len = sizeof(g_spi_nss_map) / sizeof(g_spi_nss_map[0]);

const ch32h4_periph_pin_t g_uart_tx_map[] = {
    { 1, PA9  ,  7 },
    { 1, PB14 ,  4 },
    { 2, PA2  ,  7 },
    { 3, PA13 ,  4 },
    { 3, PB10 ,  7 },
    { 3, PC10 ,  7 },
    { 4, PC6  ,  7 },
    { 4, PF4  ,  7 },
    { 5, PE0  ,  4 },
    { 5, PE3  , 11 },
    { 6, PA0  ,  8 },
    { 6, PA12 ,  6 },
    { 6, PB9  ,  8 },
    { 6, PC10 ,  8 },
    { 7, PB13 , 14 },
    { 8, PA15 , 11 },
    { 8, PB4  , 11 },
};
const size_t g_uart_tx_map_len = sizeof(g_uart_tx_map) / sizeof(g_uart_tx_map[0]);

const ch32h4_periph_pin_t g_uart_rx_map[] = {
    { 1, PA10 ,  7 },
    { 1, PB7  ,  7 },
    { 1, PB15 ,  4 },
    { 2, PA3  ,  7 },
    { 3, PA14 ,  4 },
    { 3, PB11 ,  7 },
    { 3, PC11 ,  7 },
    { 4, PC7  ,  7 },
    { 4, PF3  ,  7 },
    { 5, PF5  ,  4 },
    { 6, PA1  ,  8 },
    { 6, PB8  ,  8 },
    { 6, PC11 ,  8 },
    { 7, PB12 , 14 },
    { 8, PB3  , 11 },
};
const size_t g_uart_rx_map_len = sizeof(g_uart_rx_map) / sizeof(g_uart_rx_map[0]);

const ch32h4_i2c_pin_t g_i2c_map[] = {
    { 1, PB6  , PB7  ,  4 },
    { 1, PB8  , PB9  ,  4 },
    { 2, PB10 , PB11 ,  4 },
    { 2, PC0  , PC1  ,  9 },
    { 3, PA14 , PA13 ,  7 },
};
const size_t g_i2c_map_len = sizeof(g_i2c_map) / sizeof(g_i2c_map[0]);
