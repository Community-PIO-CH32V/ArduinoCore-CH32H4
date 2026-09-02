#include "Arduino.h"

static uint8_t s_read_bits = 10;    /* Arduino's default */
static uint8_t s_write_bits = 8;
static int s_adc_ready = 0;

#define ADC_VREF_NOMINAL_MV  1200u  /* ADC1_IN17, the internal reference */

/* The converter is 12-bit. analogRead() shifts to whatever width was asked
 * for, so the Arduino default of 10 throws away two bits -- a sketch that
 * wants the part's real resolution calls analogReadResolution(12) first.
 *
 * Above 12 the extra bits are zeros: the value is scaled, not interpolated,
 * which is what Arduino and STM32duino both do. A rejected argument leaves the
 * previous setting in place rather than clamping, so a sketch asking for
 * something impossible reads at a width it chose earlier rather than at one
 * this function picked for it.
 *
 * This affects analogRead() ONLY. ADCInput delivers what the DMA moved out of
 * the data register, which is always 12-bit, because rescaling every sample of
 * a 100 kHz capture to throw information away would be a strange default. */
void analogReadResolution(int bits) {
    if (bits >= 1 && bits <= 16) {
        s_read_bits = (uint8_t)bits;
    }
}

/* What analogRead() is currently returning, in bits. Arduino has no such
 * call; a library that wants to scale a reading otherwise has to be told the
 * width out of band, and getting it wrong is a factor-of-four error that
 * still looks like a plausible reading. */
int analogReadResolutionBits(void) {
    return (int)s_read_bits;
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

/* Set while ADCInput owns ADC1 for a timer-paced capture. */
static volatile int s_adc_capturing = 0;

void ch32h4_adc_claim(void)   { s_adc_capturing = 1; }
void ch32h4_adc_release(void) { s_adc_capturing = 0; }
int  ch32h4_adc_is_capturing(void) { return s_adc_capturing; }

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

}

/* Put ADC1 back into the single, software-triggered, one-channel mode that
 * analogRead() needs -- on EVERY read, not once.
 *
 * ADCInput drives the same ADC and leaves it in scan mode, with an external
 * trigger and DMA enabled and a whole channel list in the regular sequence.
 * A software conversion started against that configuration converts the WHOLE
 * sequence and signals EOC at the end of it, so analogRead() returns the last
 * channel of somebody else's scan. It is a real reading of a real channel,
 * just not the one that was asked for -- which is why it went unnoticed until
 * a test read a known reference and got a plausible number back.
 *
 * The cost is a handful of register writes against a conversion that already
 * takes about 20 us, and it makes analogRead() correct regardless of what ran
 * before it.
 */
static void adc_single_shot_config(void) {
    ADC_InitTypeDef a = {0};
    a.ADC_Mode = ADC_Mode_Independent;
    a.ADC_ScanConvMode = DISABLE;
    a.ADC_ContinuousConvMode = DISABLE;
    a.ADC_ExternalTrigConv = ADC_ExternalTrigConv_None;
    a.ADC_DataAlign = ADC_DataAlign_Right;
    a.ADC_NbrOfChannel = 1;
    ADC_Init(ADC1, &a);

    ADC_DMACmd(ADC1, DISABLE);
    ADC_ExternalTrigConvCmd(ADC1, DISABLE);

    /* The temperature sensor and the reference channel are off after a reset,
     * and ADCInput resets the block. */
    ADC_TempSensorVrefintCmd(ENABLE);

    ADC_Cmd(ADC1, ENABLE);
}

static uint16_t adc_read_channel(uint8_t ch) {
    adc_init_once();
    adc_single_shot_config();
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

/* The one place that decides what an "analog pin" means.
 *
 * Both real pins and the two internal channels come through here, so
 * analogRead(), ADCInput and anything else added later cannot disagree about
 * which channel a pin selects -- a disagreement that would show up as a
 * capture quietly reading the wrong input, which is close to undebuggable
 * from the outside. Returns 0xFF for anything with no ADC input at all. */
uint8_t ch32h4_adc_channel(pin_size_t pin) {
    if (pin == ATEMP) {
        return ADC_INTERNAL_TEMP_CHANNEL;
    }
    if (pin == AVREF) {
        return ADC_INTERNAL_VREF_CHANNEL;
    }
    if (pin >= PINS_COUNT) {
        return 0xFF;
    }
    return g_pins[pin].adc_channel;
}

bool ch32h4_adc_is_internal(pin_size_t pin) {
    return pin == ATEMP || pin == AVREF;
}

/* Put a pin into analog mode, if it is a pin at all. The internal channels
 * have no pad to configure, and indexing g_pins with their numbers would run
 * off the end of the table. */
void ch32h4_adc_prepare_pin(pin_size_t pin) {
    if (ch32h4_adc_is_internal(pin) || pin >= PINS_COUNT) {
        return;
    }
    const ch32h4_pin_t *p = &g_pins[pin];
    /* Analog mode: no AF mux, and the mode register's analog encoding
     * disconnects the digital input buffer. */
    ch32h4_pin_af(p->port, p->bit, CH32H4_AF_NONE, CH32H4_CFG_IN_ANALOG);
}

int analogRead(pin_size_t pin) {
    const uint8_t ch = ch32h4_adc_channel(pin);
    if (ch == 0xFF) {
        return 0;
    }
    /* There is one ADC. While ADCInput is running it owns the regular
     * sequence, and a software conversion started underneath it would both
     * return the wrong channel here and drop a scan there -- and a dropped
     * scan puts every later sample of the capture on the wrong channel.
     * Refusing is the only honest answer. */
    if (s_adc_capturing) {
        return 0;
    }
    ch32h4_adc_prepare_pin(pin);

    uint32_t raw = adc_read_channel(ch);               /* 12-bit */
    if (s_read_bits >= 12) {
        return (int)(raw << (s_read_bits - 12));
    }
    return (int)(raw >> (12 - s_read_bits));
}

/* Degrees Celsius from the on-die sensor.
 *
 * The calibration is a factory pair stored at 0x1FFFF76C -- the sensor's
 * output in millivolts, and the temperature it was measured at -- and the
 * slope is a constant 4.3 mV per degree. The slope is NEGATIVE: the sensor's
 * output falls as the die warms, which is why the difference is subtracted.
 *
 * The dummy read of main flash before touching the calibration word is
 * WCH's own sequence (TempSensor_Volt_To_Temper does the same through
 * FLASH_BOOT_GetMode), and without it the system area does not read back
 * reliably. Copied deliberately rather than called, because the SDK's
 * function takes millivolts and returns whole degrees, and rounding the
 * measurement to an integer before the division throws away most of the
 * resolution the sensor has.
 *
 * This measures the DIE, not the room. It sits several degrees above ambient
 * on a part running at 400 MHz, and it is not a thermometer -- WCH specifies
 * it for measuring CHANGES in temperature, and the absolute figure carries
 * the error of both VDDA and the single-point calibration.
 */
float analogReadTemp(void) {
    const float vdda = ch32h4_vdda_volts();
    if (vdda <= 0.0f) {
        return 0.0f;
    }
    const uint32_t raw = adc_read_channel(ADC_INTERNAL_TEMP_CHANNEL);
    if (raw == 0) {
        return 0.0f;
    }
    const float mv = (float)raw * vdda * 1000.0f / 4095.0f;

    (void)FLASH_BOOT_GetMode();
    const uint32_t cal = *(volatile uint32_t *)0x1FFFF76CU;
    const float cal_mv = (float)(cal & 0xFFFFu);
    const float cal_c = (float)((cal >> 16) & 0xFFFFu);
    if (cal_mv == 0.0f || cal_mv == 65535.0f) {
        /* An unprogrammed calibration word. Reporting a wildly wrong
         * temperature would be worse than reporting none. */
        return 0.0f;
    }

    return cal_c - (mv - cal_mv) / 4.3f;
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

    /* A DAC-capable pin goes to the DAC, not to a timer.
     *
     * This is what Arduino cores do -- STM32duino checks its DAC pin map
     * first and the SAMD core does the same -- and it is why there is no
     * dacWrite() here: a sketch written for either of those, or for an
     * Arduino Zero, works unchanged.
     *
     * The cost is real and worth stating: PA4 and PA5 both have timer
     * channels, and neither can produce PWM through analogWrite() any more.
     * PA5 in particular is the SPI1 clock and one of only two pins on the
     * 3.3 V rail with a timer. A sketch that genuinely wants PWM there can
     * still have it through ch32h4_pwm_find() and the timer API; what it
     * cannot have is analogWrite() guessing which one was meant.
     *
     * Scaling follows analogWriteResolution() exactly as the PWM path does,
     * so analogWrite(DAC1, 255) at the default 8 bits is full scale and the
     * same call after analogWriteResolution(12) is one sixteenth of it. */
    if (ch32h4_pin_has_dac(pin)) {
        const uint32_t dac_top = (1u << s_write_bits) - 1u;
        if (value < 0) {
            value = 0;
        } else if ((uint32_t)value > dac_top) {
            value = (int)dac_top;
        }
        /* Rescaled to the converter's 12 bits. Widening multiplies by
         * 4095/top rather than shifting, so full scale at any resolution is
         * full scale here and not 4080. */
        const uint16_t code = (uint16_t)(((uint32_t)value * 4095u + dac_top / 2u)
                                         / dac_top);
        ch32h4_dac_write(pin, code);
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
    /* Symmetric with analogWrite(): a DAC pin never reached the timer path,
     * so releasing a timer for it would be releasing one it never took. */
    if (ch32h4_pin_has_dac(pin)) {
        ch32h4_dac_stop(pin);
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
