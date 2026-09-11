/* Peripheral pin maps for the CH32H416RDU6. GENERATED -- see tools/genvariants.py.
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
 *   g_i2c_map          4 kept,   4 dropped
 *   g_pwm_af_map      65 kept,  62 dropped
 *   g_spi_miso_map     6 kept,   5 dropped
 *   g_spi_mosi_map     6 kept,   8 dropped
 *   g_spi_nss_map      0 kept,   1 dropped
 *   g_spi_sck_map      8 kept,   8 dropped
 *   g_uart_rx_map     13 kept,  13 dropped
 *   g_uart_tx_map     14 kept,  12 dropped */
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
    { PA11 ,  1, 4, false,  1 },
    { PA15 ,  2, 1, false,  1 },
    { PB0  ,  3, 3, false,  2 },
    { PB0  ,  5, 4, false,  4 },
    { PB0  ,  1, 2, true ,  1 },
    { PB0  ,  8, 2, true ,  3 },
    { PB1  ,  1, 3, true ,  1 },
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
    { PC11 ,  9, 4, false,  2 },
    { PC12 ,  9, 3, false,  2 },
    { PD3  , 11, 1, false,  2 },
    { PE9  ,  1, 1, false,  1 },
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

const ch32h4_periph_pin_t g_spi_sck_map[] = {
    { 1, PA5  ,  5 },
    { 1, PB3  ,  5 },
    { 1, PF7  ,  3 },
    { 2, PD3  ,  5 },
    { 2, PA12 ,  5 },
    { 3, PB3  ,  6 },
    { 3, PC10 ,  6 },
    { 3, PA14 ,  1 },
};
const size_t g_spi_sck_map_len = sizeof(g_spi_sck_map) / sizeof(g_spi_sck_map[0]);

const ch32h4_periph_pin_t g_spi_miso_map[] = {
    { 1, PA6  ,  5 },
    { 1, PB4  ,  5 },
    { 1, PF9  ,  3 },
    { 2, PC2  ,  5 },
    { 3, PC11 ,  6 },
    { 3, PB4  ,  6 },
};
const size_t g_spi_miso_map_len = sizeof(g_spi_miso_map) / sizeof(g_spi_miso_map[0]);

const ch32h4_periph_pin_t g_spi_mosi_map[] = {
    { 1, PA7  ,  5 },
    { 1, PB5  ,  5 },
    { 1, PF8  ,  3 },
    { 2, PC3  ,  5 },
    { 3, PC12 ,  6 },
    { 3, PA13 ,  1 },
};
const size_t g_spi_mosi_map_len = sizeof(g_spi_mosi_map) / sizeof(g_spi_mosi_map[0]);

const ch32h4_periph_pin_t g_spi_nss_map[] = {
};
const size_t g_spi_nss_map_len = sizeof(g_spi_nss_map) / sizeof(g_spi_nss_map[0]);

const ch32h4_periph_pin_t g_uart_tx_map[] = {
    { 1, PB6  ,  7 },
    { 2, PA2  ,  7 },
    { 3, PA13 ,  4 },
    { 3, PC10 ,  7 },
    { 4, PC6  ,  7 },
    { 6, PA0  ,  8 },
    { 6, PA12 ,  6 },
    { 6, PB9  ,  8 },
    { 6, PC10 ,  8 },
    { 7, PB6  , 14 },
    { 7, PC12 ,  8 },
    { 8, PA15 , 11 },
    { 8, PB4  , 11 },
    { 8, PF7  ,  7 },
};
const size_t g_uart_tx_map_len = sizeof(g_uart_tx_map) / sizeof(g_uart_tx_map[0]);

const ch32h4_periph_pin_t g_uart_rx_map[] = {
    { 1, PB7  ,  7 },
    { 2, PA3  ,  7 },
    { 3, PA14 ,  4 },
    { 3, PC11 ,  7 },
    { 3, PD9  ,  7 },
    { 4, PC7  ,  7 },
    { 6, PA1  ,  8 },
    { 6, PA11 ,  6 },
    { 6, PB8  ,  8 },
    { 6, PC11 ,  8 },
    { 7, PD2  ,  8 },
    { 8, PB3  , 11 },
    { 8, PF6  ,  7 },
};
const size_t g_uart_rx_map_len = sizeof(g_uart_rx_map) / sizeof(g_uart_rx_map[0]);

const ch32h4_i2c_pin_t g_i2c_map[] = {
    { 1, PB6  , PB7  ,  4 },
    { 1, PB8  , PB9  ,  4 },
    { 3, PA14 , PA13 ,  7 },
    { 4, PF12 , PF13 ,  2 },
};
const size_t g_i2c_map_len = sizeof(g_i2c_map) / sizeof(g_i2c_map[0]);
