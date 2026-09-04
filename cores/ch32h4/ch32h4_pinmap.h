/* Pin -> peripheral alternate-function maps.
 *
 * The tables themselves live in the variant (variants/<board>/pin_map.c),
 * because which pads exist is a property of the package and a board may
 * withhold one the silicon would allow. The core only knows how to search
 * them.
 *
 * Everything here is a lookup, not a claim. Claiming the timer is
 * ch32h4_timer_claim()'s job, and the two are separate so a caller can ask
 * "could this pin do PWM?" without taking anything.
 */
#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "api/Common.h"
#include "ch32h417.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t pin;      /* Arduino pin number */
    uint8_t timer;    /* 1-12 */
    uint8_t channel;  /* 1-4 */
    bool    negated;  /* the pad is TIMx_CHyN, the complementary output */
    uint8_t af;       /* alternate-function number for GPIO_PinAFConfig */
} ch32h4_pwm_af_t;

/* Provided by the variant. */
extern const ch32h4_pwm_af_t g_pwm_af_map[];
extern const size_t g_pwm_af_map_len;

/* The best PWM option for a pin.
 *
 * Prefers, in order: a timer already owned by PWM (so a second analogWrite()
 * pin joins a timer that is already running rather than taking a new one), then
 * any free timer. Negated pads are never chosen automatically -- they share a
 * compare register with their non-negated twin and idle inverted, so picking
 * one silently would give a caller an inverted output for no visible reason.
 *
 * Returns false if the pin has no timer channel, or if every timer it could use
 * is owned by something else.
 */
bool ch32h4_pwm_find(pin_size_t pin, ch32h4_pwm_af_t *out);

/* The entry for one specific timer and channel, negated pads included.
 * For callers that know exactly what they want -- Servo pinning itself to a
 * timer, or a driver that deliberately wants the complementary output. */
bool ch32h4_pwm_find_on_timer(pin_size_t pin, uint8_t timer, uint8_t channel,
                              ch32h4_pwm_af_t *out);

/* The entry for a pin whose timer is currently owned by PWM -- i.e. the one
 * analogWrite() actually configured. Used to release the right channel again.
 * Returns false if nothing is driving this pin. */
bool ch32h4_pwm_find_active(pin_size_t pin, ch32h4_pwm_af_t *out);

/* Whether a pin has any timer channel at all. Cheap; for a library that wants
 * to validate its configuration before touching hardware. */
bool ch32h4_pin_has_pwm(pin_size_t pin);

/* ---- SPI ---------------------------------------------------------------- */

/* One row per (peripheral, pin) an SPI signal can use. Signals are listed
 * separately because a pin's alternate function differs between them.
 *
 * SPI1 is on HB2; SPI2, SPI3 and SPI4 are on HB1. That split is the OPPOSITE
 * way round from I2C, where 1-3 are on HB1 and only I2C4 is on HB2, and it is
 * exactly the kind of thing that produces a peripheral whose registers read
 * back as zeroes with no error at all. ch32h4_spi_clock_enable() owns it so no
 * caller has to remember. */
typedef struct {
    uint8_t id;    /* 1-4 */
    uint8_t pin;
    uint8_t af;
} ch32h4_periph_pin_t;

extern const ch32h4_periph_pin_t g_spi_sck_map[];
extern const size_t g_spi_sck_map_len;
extern const ch32h4_periph_pin_t g_spi_miso_map[];
extern const size_t g_spi_miso_map_len;
extern const ch32h4_periph_pin_t g_spi_mosi_map[];
extern const size_t g_spi_mosi_map_len;

/* The alternate function for `pin` on SPI `id`, or false if that pin cannot
 * carry that signal for that peripheral. */
bool ch32h4_spi_sck_af(uint8_t id, pin_size_t pin, uint8_t *af);
bool ch32h4_spi_miso_af(uint8_t id, pin_size_t pin, uint8_t *af);
bool ch32h4_spi_mosi_af(uint8_t id, pin_size_t pin, uint8_t *af);

/* ---- USART --------------------------------------------------------------
 *
 * Same shape as the SPI maps, and the same warning applies twice over: a wrong
 * AF number gives a USART that initialises, reports TXE and TC set, holds the
 * right divisor in BRR -- and puts nothing on the wire.
 *
 * USART1 is on HB2; USART2 through USART8 are on HB1. That is the opposite
 * split from SPI, where only SPI1 is on HB2, and matching neither is what
 * ch32h4_uart_clock_enable() is for.
 */
extern const ch32h4_periph_pin_t g_uart_tx_map[];
extern const size_t g_uart_tx_map_len;
extern const ch32h4_periph_pin_t g_uart_rx_map[];
extern const size_t g_uart_rx_map_len;

/* The alternate function for `pin` on USART `id` (1-8), or false if that pin
 * cannot carry that signal for that peripheral. */
bool ch32h4_uart_tx_af(uint8_t id, pin_size_t pin, uint8_t *af);
bool ch32h4_uart_rx_af(uint8_t id, pin_size_t pin, uint8_t *af);

/* Enable a USART's clock on the correct bus. */
void ch32h4_uart_clock_enable(uint8_t id);

/* Reset a USART's block, on the correct bus. Configuration survives a warm
 * reset and a re-flash, so a port that is not reset inherits the previous
 * run's baud rate and mode. */
void ch32h4_uart_reset(uint8_t id);

/* The peripheral for a USART id, or NULL. */
USART_TypeDef *ch32h4_uart_dev(uint8_t id);

/* The NVIC line for a USART id, or a negative value if there is none. */
int ch32h4_uart_irqn(uint8_t id);

/* Which SPI peripheral this trio of pins can all serve, or 0. */
uint8_t ch32h4_spi_find(pin_size_t sck, pin_size_t miso, pin_size_t mosi);

/* ---- I2C ---------------------------------------------------------------- */

/* SCL and SDA are paired here rather than listed separately, because the
 * silicon pairs them: a peripheral's SCL on one pad implies its SDA on a
 * specific other pad, and mixing pairs does not work. */
typedef struct {
    uint8_t id;    /* 1-4 */
    uint8_t scl;
    uint8_t sda;
    uint8_t af;
} ch32h4_i2c_pin_t;

extern const ch32h4_i2c_pin_t g_i2c_map[];
extern const size_t g_i2c_map_len;

/* Which I2C peripheral serves this SCL/SDA pair, and on which alternate
 * function. Returns 0 if the pair is not one the silicon offers. */
uint8_t ch32h4_i2c_find(pin_size_t scl, pin_size_t sda, uint8_t *af);

#ifdef __cplusplus
}
#endif
