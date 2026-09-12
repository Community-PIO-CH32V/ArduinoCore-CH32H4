/* Peripheral pin maps for the CH32H417WEU6. GENERATED -- see tools/genvariants.py.
 *
 * Derived from the CH32H417QEU6 maps, which came from the MicroPython port for
 * this silicon and have run on hardware, by keeping only the entries this part
 * can actually perform: the pins must be bonded on the QFN68 package, and
 * this part's own datasheet function list must confirm the signal at that
 * alternate-function number.
 *
 * An entry the part's table does not confirm is DROPPED rather than adjusted.
 * The table can therefore be incomplete, but it cannot point a peripheral at
 * the wrong pad, which is the failure that would be invisible.
 *
 * Entries kept, by table:
 *   g_i2c_map          5 kept,   3 dropped
 *   g_pwm_af_map      64 kept,  63 dropped
 *   g_spi_miso_map     6 kept,   5 dropped
 *   g_spi_mosi_map     7 kept,   7 dropped
 *   g_spi_nss_map      0 kept,   1 dropped
 *   g_spi_sck_map      5 kept,  11 dropped
 *   g_uart_rx_map     13 kept,  13 dropped
 *   g_uart_tx_map     12 kept,  14 dropped */
#include "Arduino.h"
#include "ch32h4_pinmap.h"

const ch32h4_pwm_af_t g_pwm_af_map[] = {
    { PA5  ,  2, 1, false,  1 },
    { PA5  ,  8, 1, true ,  3 },
    { PA15 ,  2, 1, false,  1 },
    { PB0  ,  3, 3, false,  2 },
    { PB0  ,  5, 4, false,  4 },
    { PB0  ,  1, 2, true ,  1 },
    { PB0  ,  8, 2, true ,  3 },
    { PB1  ,  3, 4, false,  2 },
    { PB1  , 12, 1, false,  5 },
    { PB1  ,  1, 3, true ,  1 },
    { PB1  ,  8, 3, true ,  3 },
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
    { PE13 ,  1, 3, false,  1 },
    { PE13 , 12, 2, false,  2 },
    { PE14 ,  1, 4, false,  1 },
    { PE14 , 12, 3, false,  2 },
    { PE15 , 12, 4, false,  2 },
};
const size_t g_pwm_af_map_len = sizeof(g_pwm_af_map) / sizeof(g_pwm_af_map[0]);

const ch32h4_periph_pin_t g_spi_sck_map[] = {
    { 1, PA5  ,  5 },
    { 1, PF5  ,  5 },
    { 2, PB13 ,  5 },
    { 2, PB10 ,  5 },
    { 3, PA14 ,  1 },
};
const size_t g_spi_sck_map_len = sizeof(g_spi_sck_map) / sizeof(g_spi_sck_map[0]);

const ch32h4_periph_pin_t g_spi_miso_map[] = {
    { 1, PF3  ,  5 },
    { 2, PB14 ,  5 },
    { 2, PC2  ,  5 },
    { 3, PC11 ,  6 },
    { 3, PC9  ,  5 },
    { 4, PE13 ,  5 },
};
const size_t g_spi_miso_map_len = sizeof(g_spi_miso_map) / sizeof(g_spi_miso_map[0]);

const ch32h4_periph_pin_t g_spi_mosi_map[] = {
    { 1, PD7  ,  5 },
    { 2, PC3  ,  5 },
    { 2, PC1  ,  5 },
    { 3, PC12 ,  6 },
    { 3, PD6  ,  5 },
    { 3, PA13 ,  1 },
    { 4, PE14 ,  5 },
};
const size_t g_spi_mosi_map_len = sizeof(g_spi_mosi_map) / sizeof(g_spi_mosi_map[0]);

const ch32h4_periph_pin_t g_spi_nss_map[] = {
};
const size_t g_spi_nss_map_len = sizeof(g_spi_nss_map) / sizeof(g_spi_nss_map[0]);

const ch32h4_periph_pin_t g_uart_tx_map[] = {
    { 1, PB14 ,  4 },
    { 1, PD13 , 14 },
    { 2, PD5  ,  7 },
    { 3, PA13 ,  4 },
    { 3, PB10 ,  7 },
    { 4, PC6  ,  7 },
    { 5, PE0  ,  4 },
    { 6, PB9  ,  8 },
    { 6, PD1  ,  8 },
    { 7, PB13 , 14 },
    { 7, PC12 ,  8 },
    { 8, PA15 , 11 },
};
const size_t g_uart_tx_map_len = sizeof(g_uart_tx_map) / sizeof(g_uart_tx_map[0]);

const ch32h4_periph_pin_t g_uart_rx_map[] = {
    { 1, PD12 , 14 },
    { 2, PD6  ,  7 },
    { 3, PA14 ,  4 },
    { 3, PB11 ,  7 },
    { 3, PC11 ,  7 },
    { 4, PC7  ,  7 },
    { 4, PF3  ,  7 },
    { 5, PF5  ,  4 },
    { 6, PB8  ,  8 },
    { 6, PC11 ,  8 },
    { 6, PD0  ,  8 },
    { 7, PB12 , 14 },
    { 7, PD2  ,  8 },
};
const size_t g_uart_rx_map_len = sizeof(g_uart_rx_map) / sizeof(g_uart_rx_map[0]);

const ch32h4_i2c_pin_t g_i2c_map[] = {
    { 1, PB8  , PB9  ,  4 },
    { 2, PB10 , PB11 ,  4 },
    { 2, PC0  , PC1  ,  9 },
    { 3, PA14 , PA13 ,  7 },
    { 4, PD12 , PD13 ,  4 },
};
const size_t g_i2c_map_len = sizeof(g_i2c_map) / sizeof(g_i2c_map[0]);
