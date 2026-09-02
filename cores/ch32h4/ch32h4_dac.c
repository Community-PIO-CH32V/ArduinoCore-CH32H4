/* The two 12-bit DACs.
 *
 * DAC1 is on PA4 and DAC2 on PA5, and those are the only pins either can
 * reach -- there is no mux and no alternative. Both convert as soon as their
 * data register is written, because nothing here uses a trigger; a sketch
 * wanting timed output would want a trigger and the DMA, which is a different
 * driver.
 *
 * Reached through analogWrite(), which is what Arduino cores do: STM32duino
 * and the SAMD core both route a DAC-capable pin to the DAC and everything
 * else to a timer. Only ESP32 has a separate dacWrite(). See the note on
 * analogWrite() in wiring_analog.c for what that costs.
 *
 * ### The output buffer
 *
 * On by default, because a buffered output can drive a real load and an
 * unbuffered one cannot -- unbuffered it is a high-impedance node that any
 * load pulls off value.
 *
 * On classic STM32 the buffer costs most of a volt of range: the output
 * cannot come within about 0.2 V of either rail, so a buffered channel told
 * to output zero sits near 250 counts. **That is not true here.** Measured
 * on this silicon, reading the DAC's own pad back through the ADC:
 *
 *              code 0      code 4095
 *   buffered      1           4090
 *   unbuffered    0           4089
 *
 * -- about one count of difference at the bottom and none worth reporting at
 * the top. The buffered output is effectively rail to rail.
 *
 * So ch32h4_dac_output_buffer() exists for drive strength, not for range,
 * and the default needs no apology. Note that this measurement cannot say
 * anything about drive strength: the ADC input is a high-impedance load, and
 * it is precisely the load an unbuffered DAC copes with best.
 */
#include "Arduino.h"

#include "ch32h4_gpio.h"
#include "ch32h4_rcc.h"

/* Per channel, index 0 = DAC1. */
static uint8_t s_dac_started[2];
static uint8_t s_dac_buffered[2] = { 1, 1 };

uint8_t ch32h4_dac_channel(pin_size_t pin) {
    if (pin == PIN_DAC1) {
        return 1;
    }
    if (pin == PIN_DAC2) {
        return 2;
    }
    return 0;
}

bool ch32h4_pin_has_dac(pin_size_t pin) {
    return ch32h4_dac_channel(pin) != 0;
}

bool ch32h4_dac_output_buffer(pin_size_t pin, bool enable) {
    const uint8_t ch = ch32h4_dac_channel(pin);
    if (ch == 0) {
        return false;
    }
    if (s_dac_buffered[ch - 1] != (uint8_t)enable) {
        s_dac_buffered[ch - 1] = (uint8_t)enable;
        /* Re-initialised on the next write, since the setting lives in the
         * control register the init call writes. */
        s_dac_started[ch - 1] = 0;
    }
    return true;
}

static void dac_start(uint8_t ch) {
    if (s_dac_started[ch - 1]) {
        return;
    }

    /* The DAC is on HB1. Habit says APB1, and there is no APB on this family.
     * ch32h4_clock_enable() reads the register back; the bare SDK call does
     * not, and a dropped clock enable presents as a peripheral whose registers
     * all read zero. */
    ch32h4_clock_enable(CH32_BUS_HB1, RCC_HB1Periph_DAC);

    /* Analog mode on the pad. Anything else leaves the digital input buffer
     * connected to a pin the DAC is driving to a mid-rail voltage, which is
     * the one input level a CMOS input stage must never be held at: it turns
     * on both halves and burns current for as long as the DAC is on. */
    const pin_size_t pin = (ch == 1) ? PIN_DAC1 : PIN_DAC2;
    ch32h4_pin_af(g_pins[pin].port, g_pins[pin].bit,
                  CH32H4_AF_NONE, CH32H4_CFG_IN_ANALOG);

    DAC_InitTypeDef init = {0};
    /* No trigger: the conversion starts when the data register is written,
     * which is what a call-and-return API wants. */
    init.DAC_Trigger = DAC_Trigger_None;
    init.DAC_WaveGeneration = DAC_WaveGeneration_None;
    init.DAC_LFSRUnmask_TriangleAmplitude = 0;
    init.DAC_OutputBuffer = s_dac_buffered[ch - 1]
                            ? DAC_OutputBuffer_Enable
                            : DAC_OutputBuffer_Disable;
    DAC_Init((ch == 1) ? DAC_Channel_1 : DAC_Channel_2, &init);
    DAC_Cmd((ch == 1) ? DAC_Channel_1 : DAC_Channel_2, ENABLE);

    s_dac_started[ch - 1] = 1;
}

bool ch32h4_dac_write(pin_size_t pin, uint16_t value12) {
    const uint8_t ch = ch32h4_dac_channel(pin);
    if (ch == 0) {
        return false;
    }
    if (value12 > 4095u) {
        value12 = 4095u;
    }
    dac_start(ch);
    if (ch == 1) {
        DAC_SetChannel1Data(DAC_Align_12b_R, value12);
    } else {
        DAC_SetChannel2Data(DAC_Align_12b_R, value12);
    }
    return true;
}

uint16_t ch32h4_dac_read(pin_size_t pin) {
    const uint8_t ch = ch32h4_dac_channel(pin);
    if (ch == 0 || !s_dac_started[ch - 1]) {
        return 0;
    }
    return DAC_GetDataOutputValue((ch == 1) ? DAC_Channel_1 : DAC_Channel_2);
}

void ch32h4_dac_stop(pin_size_t pin) {
    const uint8_t ch = ch32h4_dac_channel(pin);
    if (ch == 0 || !s_dac_started[ch - 1]) {
        return;
    }
    DAC_Cmd((ch == 1) ? DAC_Channel_1 : DAC_Channel_2, DISABLE);
    s_dac_started[ch - 1] = 0;

    /* Left in analog mode rather than returned to a floating input. The pad
     * is an ADC input as well, and analog mode is the right state for that;
     * a sketch wanting it as a GPIO calls pinMode(). */
}

bool ch32h4_dac_is_started(pin_size_t pin) {
    const uint8_t ch = ch32h4_dac_channel(pin);
    return ch != 0 && s_dac_started[ch - 1] != 0;
}
