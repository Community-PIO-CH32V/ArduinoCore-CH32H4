#include "Arduino.h"

static uint8_t s_read_bits = 10;    /* Arduino's default */
static uint8_t s_write_bits = 8;
static int s_adc_ready = 0;

#define ADC_VREF_NOMINAL_MV  1200u  /* ADC1_IN17, the internal reference */

void analogReadResolution(int bits) {
    if (bits >= 1 && bits <= 16) {
        s_read_bits = (uint8_t)bits;
    }
}

void analogWriteResolution(int bits) {
    if (bits >= 1 && bits <= 16) {
        s_write_bits = (uint8_t)bits;
    }
}

/* VDDA is the only reference this part has. Provided so sketches link. */
void analogReference(uint8_t mode) {
    (void)mode;
}

static void adc_init_once(void) {
    if (s_adc_ready) {
        return;
    }
    s_adc_ready = 1;

    /* ADC1 is on HB2. Habit says APB2, and there is no APB on this family. */
    ch32h4_clock_enable(CH32_BUS_HB2, RCC_HB2Periph_ADC1);
    ch32h4_block_reset(CH32_BUS_HB2, RCC_HB2Periph_ADC1);

    /* ADCPRE, written by hand.
     *
     * RCC->CFGR0 bits [15:14] are ADCPRE, but the SDK's RCC_ADCPRE_ADCH_DIVx
     * constants sit at bits [13:12] and disagree both with RCC_ADCPRE's own
     * mask and with what RCC_ADCHCLKCLKAsSourceConfig() writes. None of the
     * three can be trusted, so the field is written directly.
     *
     * 0b11 selects HCLK/8 = 12.5 MHz, which is the divider the prior ports
     * measured this part running at. */
    uint32_t cfgr = RCC->CFGR0;
    cfgr &= ~(3u << 14);
    cfgr |= (3u << 14);
    RCC->CFGR0 = cfgr;

    ADC_InitTypeDef a = {0};
    a.ADC_Mode = ADC_Mode_Independent;
    a.ADC_ScanConvMode = DISABLE;
    a.ADC_ContinuousConvMode = DISABLE;
    a.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    a.ADC_DataAlign = ADC_DataAlign_Right;
    a.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &a);

    /* The internal reference channel has to be switched on explicitly. */
    ADC_TempSensorVrefintCmd(ENABLE);

    ADC_Cmd(ADC1, ENABLE);
}

static uint16_t adc_read_channel(uint8_t ch) {
    adc_init_once();
    /* The longest sample time this part offers. The SDK names them
     * CyclesMode0..7 rather than by cycle count, so there is no way to read
     * the duration off the constant -- 7 is the slowest. It is deliberate: at
     * 12.5 MHz ADCCLK the internal reference in particular needs a long
     * sample, and nothing here is rate-critical. */
    ADC_RegularChannelConfig(ADC1, ch, 1, ADC_SampleTime_CyclesMode7);
    ADC_SoftwareStartConvCmd(ADC1, ENABLE);

    uint32_t guard = 1000000u;
    while (ADC_GetFlagStatus(ADC1, ADC_FLAG_EOC) == RESET && --guard) {
    }
    if (guard == 0) {
        return 0;
    }
    return ADC_GetConversionValue(ADC1);
}

int analogRead(pin_size_t pin) {
    if (pin >= PINS_COUNT) {
        return 0;
    }
    const ch32h4_pin_t *p = &g_pins[pin];
    if (p->adc_channel == 0xFF) {
        return 0;
    }

    /* Analog mode: no AF mux, and the mode register's analog encoding
     * disconnects the digital input buffer. */
    ch32h4_pin_af(p->port, p->bit, CH32H4_AF_NONE, CH32H4_CFG_IN_ANALOG);

    uint32_t raw = adc_read_channel(p->adc_channel);   /* 12-bit */
    if (s_read_bits >= 12) {
        return (int)(raw << (s_read_bits - 12));
    }
    return (int)(raw >> (12 - s_read_bits));
}

float ch32h4_vdda_volts(void) {
    /* The internal reference is the check on the assumption that VDDA is
     * 3.3 V. If VDDA is what we think, a nominally 1.20 V input reads
     * 4095 * 1.2 / 3.3 = 1489 counts; solving for VDDA gives this. */
    uint32_t raw = adc_read_channel(ADC_INTERNAL_VREF_CHANNEL);
    if (raw == 0) {
        return 0.0f;
    }
    return (4095.0f * (float)ADC_VREF_NOMINAL_MV) / ((float)raw * 1000.0f);
}

/* analogWrite on any pin the package gives a timer channel -- 67 of them on
 * this one, across ten timers.
 *
 * The pin -> (timer, channel, AF) map lives in the variant; the timer
 * ownership registry lives in ch32h4_timer.c. This function only joins the
 * two, which is why Servo, tone() and I2S can contend for the same timers
 * later without any of them knowing about each other.
 *
 * Frequency is per TIMER, not per pin, because the period register is shared
 * by all four channels. Two analogWrite() pins that land on one timer
 * therefore share a frequency -- which is standard Arduino behaviour, and is
 * why the search prefers a timer PWM already owns rather than spreading across
 * all twelve.
 */

static uint32_t s_pwm_hz = 1000;                 /* Arduino's usual ballpark */
static uint8_t  s_pwm_chan_active[CH32H4_TIMER_COUNT + 1];

void analogWriteFrequency(uint32_t hz) {
    if (hz > 0) {
        s_pwm_hz = hz;
    }
}

/* Configure the timer's time base for the current resolution and frequency.
 * Called for the first channel on a timer, and again if the resolution or
 * frequency changes -- all channels on the timer follow. */
static void pwm_configure_timebase(uint8_t timer, uint32_t top) {
    TIM_TypeDef *dev = ch32h4_timer_dev(timer);

    /* Timers divide HCLK. SystemCoreClock is four times that on this core and
     * is never the right number for a divider. */
    uint32_t prescaler = ch32h4_timer_input_clock(timer) / (s_pwm_hz * (top + 1u));
    if (prescaler == 0) {
        prescaler = 1;
    }
    if (prescaler > 0x10000u) {
        prescaler = 0x10000u;
    }

    TIM_TimeBaseInitTypeDef t = {0};
    t.TIM_Prescaler = (uint16_t)(prescaler - 1u);
    t.TIM_CounterMode = TIM_CounterMode_Up;
    t.TIM_Period = (uint16_t)top;
    t.TIM_ClockDivision = TIM_CKD_DIV1;
    t.TIM_RepetitionCounter = 0;
    TIM_TimeBaseInit(dev, &t);
    TIM_ARRPreloadConfig(dev, ENABLE);
}

static void pwm_set_channel(TIM_TypeDef *dev, uint8_t channel, uint16_t pulse,
                            bool negated) {
    TIM_OCInitTypeDef oc = {0};
    oc.TIM_OCMode = TIM_OCMode_PWM1;
    oc.TIM_OutputState = negated ? TIM_OutputState_Disable : TIM_OutputState_Enable;
    oc.TIM_OutputNState = negated ? TIM_OutputNState_Enable : TIM_OutputNState_Disable;
    oc.TIM_Pulse = pulse;
    oc.TIM_OCPolarity = TIM_OCPolarity_High;
    oc.TIM_OCNPolarity = TIM_OCNPolarity_High;
    oc.TIM_OCIdleState = TIM_OCIdleState_Reset;
    oc.TIM_OCNIdleState = TIM_OCNIdleState_Reset;

    switch (channel) {
        case 1: TIM_OC1Init(dev, &oc); TIM_OC1PreloadConfig(dev, TIM_OCPreload_Enable); break;
        case 2: TIM_OC2Init(dev, &oc); TIM_OC2PreloadConfig(dev, TIM_OCPreload_Enable); break;
        case 3: TIM_OC3Init(dev, &oc); TIM_OC3PreloadConfig(dev, TIM_OCPreload_Enable); break;
        default: TIM_OC4Init(dev, &oc); TIM_OC4PreloadConfig(dev, TIM_OCPreload_Enable); break;
    }
}

void analogWrite(pin_size_t pin, int value) {
    if (pin >= PINS_COUNT) {
        return;
    }

    ch32h4_pwm_af_t af;
    if (!ch32h4_pwm_find(pin, &af)) {
        /* Either the pin has no timer channel at all, or every timer it could
         * use is held by tone(), Servo or I2S. Silently doing nothing is the
         * Arduino convention for a bad pin; ch32h4_pin_has_pwm() and
         * ch32h4_timer_owner() let a caller find out which it was. */
        return;
    }

    if (!ch32h4_timer_claim(af.timer, CH32H4_TIMER_PWM)) {
        return;
    }

    const uint32_t top = (1u << s_write_bits) - 1u;
    if (value < 0) {
        value = 0;
    } else if ((uint32_t)value > top) {
        value = (int)top;
    }

    TIM_TypeDef *dev = ch32h4_timer_dev(af.timer);

    /* First channel on this timer: bring the block up and set the time base.
     * Later channels join the running timer and only set their own compare. */
    const bool first = (s_pwm_chan_active[af.timer] == 0);
    if (first) {
        ch32h4_timer_clock_enable(af.timer);
        ch32h4_timer_reset(af.timer);
        pwm_configure_timebase(af.timer, top);
    }
    s_pwm_chan_active[af.timer] |= (uint8_t)(1u << (af.channel - 1u));

    ch32h4_pin_af(g_pins[pin].port, g_pins[pin].bit, af.af, CH32H4_CFG_AF_PP_50);
    pwm_set_channel(dev, af.channel, (uint16_t)value, af.negated);

    /* TIM1 and TIM8 are advanced-control timers: their outputs stay
     * electrically disabled until MOE is set, and nothing says so. The other
     * timers ignore this call. */
    if (af.timer == 1 || af.timer == 8) {
        TIM_CtrlPWMOutputs(dev, ENABLE);
    }

    TIM_Cmd(dev, ENABLE);
}

/* Stop driving a pin and, once a timer has no channels left, give it back so
 * Servo or tone() can have it. Arduino has no analogWriteStop(), but every
 * library that shares these timers needs one. */
void analogWriteStop(pin_size_t pin) {
    if (pin >= PINS_COUNT) {
        return;
    }
    ch32h4_pwm_af_t af;
    if (!ch32h4_pwm_find_active(pin, &af)) {
        return;
    }
    if (ch32h4_timer_owner(af.timer) != CH32H4_TIMER_PWM) {
        return;
    }

    s_pwm_chan_active[af.timer] &= (uint8_t)~(1u << (af.channel - 1u));
    pinMode(pin, INPUT);

    if (s_pwm_chan_active[af.timer] == 0) {
        TIM_Cmd(ch32h4_timer_dev(af.timer), DISABLE);
        ch32h4_timer_release(af.timer, CH32H4_TIMER_PWM);
    }
}
