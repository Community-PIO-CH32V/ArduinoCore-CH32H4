#include "Arduino.h"
#include "ch32h4_pinmap.h"
#include "ch32h4_timer.h"
#include "ch32h4_clock.h"

bool ch32h4_pin_has_pwm(pin_size_t pin) {
    for (size_t i = 0; i < g_pwm_af_map_len; i++) {
        if (g_pwm_af_map[i].pin == pin) {
            return true;
        }
    }
    return false;
}

bool ch32h4_pwm_find_on_timer(pin_size_t pin, uint8_t timer, uint8_t channel,
                              ch32h4_pwm_af_t *out) {
    for (size_t i = 0; i < g_pwm_af_map_len; i++) {
        const ch32h4_pwm_af_t *e = &g_pwm_af_map[i];
        if (e->pin == pin && e->timer == timer && e->channel == channel) {
            if (out) {
                *out = *e;
            }
            return true;
        }
    }
    return false;
}

bool ch32h4_pwm_find(pin_size_t pin, ch32h4_pwm_af_t *out) {
    const ch32h4_pwm_af_t *first_free = NULL;

    for (size_t i = 0; i < g_pwm_af_map_len; i++) {
        const ch32h4_pwm_af_t *e = &g_pwm_af_map[i];
        if (e->pin != pin) {
            continue;
        }
        /* Never chosen automatically: a complementary output shares its
         * compare register with its twin and idles inverted, so handing one
         * back silently would give the caller an inverted signal with nothing
         * to explain it. ch32h4_pwm_find_on_timer() will still return one. */
        if (e->negated) {
            continue;
        }

        const uint8_t owner = ch32h4_timer_owner(e->timer);

        /* Prefer a timer PWM already holds, so a second analogWrite() pin
         * joins the running timer instead of consuming another one. Twelve
         * timers go quickly when every call takes a fresh one. */
        if (owner == CH32H4_TIMER_PWM) {
            if (out) {
                *out = *e;
            }
            return true;
        }
        if (owner == CH32H4_TIMER_FREE && first_free == NULL) {
            first_free = e;
        }
    }

    if (first_free) {
        if (out) {
            *out = *first_free;
        }
        return true;
    }
    return false;
}

bool ch32h4_pwm_find_active(pin_size_t pin, ch32h4_pwm_af_t *out) {
    for (size_t i = 0; i < g_pwm_af_map_len; i++) {
        const ch32h4_pwm_af_t *e = &g_pwm_af_map[i];
        if (e->pin == pin && ch32h4_timer_owner(e->timer) == CH32H4_TIMER_PWM) {
            if (out) {
                *out = *e;
            }
            return true;
        }
    }
    return false;
}

/* ---- SPI ---------------------------------------------------------------- */

static bool find_periph_af(const ch32h4_periph_pin_t *map, size_t len,
                           uint8_t id, pin_size_t pin, uint8_t *af) {
    for (size_t i = 0; i < len; i++) {
        if (map[i].id == id && map[i].pin == pin) {
            if (af) {
                *af = map[i].af;
            }
            return true;
        }
    }
    return false;
}

bool ch32h4_spi_sck_af(uint8_t id, pin_size_t pin, uint8_t *af) {
    return find_periph_af(g_spi_sck_map, g_spi_sck_map_len, id, pin, af);
}

bool ch32h4_spi_miso_af(uint8_t id, pin_size_t pin, uint8_t *af) {
    return find_periph_af(g_spi_miso_map, g_spi_miso_map_len, id, pin, af);
}

bool ch32h4_spi_mosi_af(uint8_t id, pin_size_t pin, uint8_t *af) {
    return find_periph_af(g_spi_mosi_map, g_spi_mosi_map_len, id, pin, af);
}

bool ch32h4_spi_nss_af(uint8_t id, pin_size_t pin, uint8_t *af) {
    return find_periph_af(g_spi_nss_map, g_spi_nss_map_len, id, pin, af);
}

uint8_t ch32h4_spi_find(pin_size_t sck, pin_size_t miso, pin_size_t mosi) {
    /* All three signals have to land on ONE peripheral. Searching per-signal
     * and hoping they agree is how you end up driving SCK from SPI1 and MOSI
     * from SPI3, which produces a clock and no data and looks like a wiring
     * fault. MISO is allowed to be unused (write-only devices are common). */
    for (uint8_t id = 1; id <= 4; id++) {
        if (!ch32h4_spi_sck_af(id, sck, NULL)) {
            continue;
        }
        if (!ch32h4_spi_mosi_af(id, mosi, NULL)) {
            continue;
        }
        if (miso != (pin_size_t)-1 && !ch32h4_spi_miso_af(id, miso, NULL)) {
            continue;
        }
        return id;
    }
    return 0;
}

/* ---- I2C ---------------------------------------------------------------- */

uint8_t ch32h4_i2c_find(pin_size_t scl, pin_size_t sda, uint8_t *af) {
    for (size_t i = 0; i < g_i2c_map_len; i++) {
        if (g_i2c_map[i].scl == scl && g_i2c_map[i].sda == sda) {
            if (af) {
                *af = g_i2c_map[i].af;
            }
            return g_i2c_map[i].id;
        }
    }
    return 0;
}

/* ---- USART --------------------------------------------------------------- */

static bool periph_af(const ch32h4_periph_pin_t *map, size_t len,
                      uint8_t id, pin_size_t pin, uint8_t *af) {
    for (size_t i = 0; i < len; i++) {
        if (map[i].id == id && map[i].pin == (uint8_t)pin) {
            if (af) {
                *af = map[i].af;
            }
            return true;
        }
    }
    return false;
}

bool ch32h4_uart_tx_af(uint8_t id, pin_size_t pin, uint8_t *af) {
    return periph_af(g_uart_tx_map, g_uart_tx_map_len, id, pin, af);
}

bool ch32h4_uart_rx_af(uint8_t id, pin_size_t pin, uint8_t *af) {
    return periph_af(g_uart_rx_map, g_uart_rx_map_len, id, pin, af);
}

/* USART1 alone is on HB2. Enabling any of the others there gives a peripheral
 * whose registers read back as zeroes, with no error anywhere -- the same trap
 * the timer and SPI helpers exist to close. */
typedef struct {
    USART_TypeDef *dev;
    ch32_bus_t     bus;
    uint32_t       mask;
    int            irqn;
} uart_hw_t;

static const uart_hw_t s_uart[9] = {
    { NULL,   CH32_BUS_HB1, 0,                      -1            },
    { USART1, CH32_BUS_HB2, RCC_HB2Periph_USART1,   USART1_IRQn   },
    { USART2, CH32_BUS_HB1, RCC_HB1Periph_USART2,   USART2_IRQn   },
    { USART3, CH32_BUS_HB1, RCC_HB1Periph_USART3,   USART3_IRQn   },
    { USART4, CH32_BUS_HB1, RCC_HB1Periph_USART4,   USART4_IRQn   },
    { USART5, CH32_BUS_HB1, RCC_HB1Periph_USART5,   USART5_IRQn   },
    { USART6, CH32_BUS_HB1, RCC_HB1Periph_USART6,   USART6_IRQn   },
    { USART7, CH32_BUS_HB1, RCC_HB1Periph_USART7,   USART7_IRQn   },
    { USART8, CH32_BUS_HB1, RCC_HB1Periph_USART8,   USART8_IRQn   },
};

static bool uart_valid(uint8_t id) {
    return id >= 1 && id <= 8;
}

USART_TypeDef *ch32h4_uart_dev(uint8_t id) {
    return uart_valid(id) ? s_uart[id].dev : NULL;
}

int ch32h4_uart_irqn(uint8_t id) {
    return uart_valid(id) ? s_uart[id].irqn : -1;
}

void ch32h4_uart_clock_enable(uint8_t id) {
    if (uart_valid(id)) {
        ch32h4_clock_enable(s_uart[id].bus, s_uart[id].mask);
    }
}

void ch32h4_uart_reset(uint8_t id) {
    if (uart_valid(id)) {
        ch32h4_block_reset(s_uart[id].bus, s_uart[id].mask);
    }
}
