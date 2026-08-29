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
