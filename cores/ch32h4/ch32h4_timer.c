#include "Arduino.h"
#include "ch32h4_timer.h"

/* Indexed 1..12; slot 0 is unused so the id and the index are the same number
 * everywhere and there is no off-by-one to get wrong. */
static uint8_t s_owner[CH32H4_TIMER_COUNT + 1];

/* TIM1 and TIM8-TIM12 are on HB2; TIM2-TIM7 are on HB1. There is no APB on
 * this family, and this split is not what habit predicts. */
typedef struct {
    TIM_TypeDef *dev;
    ch32_bus_t   bus;
    uint32_t     mask;
} timer_hw_t;

static const timer_hw_t s_hw[CH32H4_TIMER_COUNT + 1] = {
    { NULL,  CH32_BUS_HB1, 0 },                              /* unused */
    { TIM1,  CH32_BUS_HB2, RCC_HB2Periph_TIM1  },
    { TIM2,  CH32_BUS_HB1, RCC_HB1Periph_TIM2  },
    { TIM3,  CH32_BUS_HB1, RCC_HB1Periph_TIM3  },
    { TIM4,  CH32_BUS_HB1, RCC_HB1Periph_TIM4  },
    { TIM5,  CH32_BUS_HB1, RCC_HB1Periph_TIM5  },
    { TIM6,  CH32_BUS_HB1, RCC_HB1Periph_TIM6  },
    { TIM7,  CH32_BUS_HB1, RCC_HB1Periph_TIM7  },
    { TIM8,  CH32_BUS_HB2, RCC_HB2Periph_TIM8  },
    { TIM9,  CH32_BUS_HB2, RCC_HB2Periph_TIM9  },
    { TIM10, CH32_BUS_HB2, RCC_HB2Periph_TIM10 },
    { TIM11, CH32_BUS_HB2, RCC_HB2Periph_TIM11 },
    { TIM12, CH32_BUS_HB2, RCC_HB2Periph_TIM12 },
};

static bool valid(uint8_t id) {
    return id >= 1 && id <= CH32H4_TIMER_COUNT;
}

bool ch32h4_timer_claim(uint8_t id, uint8_t owner) {
    if (!valid(id) || owner == CH32H4_TIMER_FREE) {
        return false;
    }
    if (s_owner[id] == CH32H4_TIMER_FREE || s_owner[id] == owner) {
        s_owner[id] = owner;
        return true;
    }
    return false;
}

void ch32h4_timer_release(uint8_t id, uint8_t owner) {
    if (valid(id) && s_owner[id] == owner) {
        s_owner[id] = CH32H4_TIMER_FREE;
    }
}

uint8_t ch32h4_timer_owner(uint8_t id) {
    return valid(id) ? s_owner[id] : CH32H4_TIMER_FREE;
}

const char *ch32h4_timer_owner_name(uint8_t owner) {
    switch (owner) {
        case CH32H4_TIMER_PWM:   return "PWM";
        case CH32H4_TIMER_TONE:  return "tone";
        case CH32H4_TIMER_SERVO: return "Servo";
        case CH32H4_TIMER_I2S:   return "I2S";
        case CH32H4_TIMER_ADC:   return "ADC";
        case CH32H4_TIMER_USER:  return "user";
        default:                 return "free";
    }
}

TIM_TypeDef *ch32h4_timer_dev(uint8_t id) {
    return valid(id) ? s_hw[id].dev : NULL;
}

void ch32h4_timer_clock_enable(uint8_t id) {
    if (valid(id)) {
        ch32h4_clock_enable(s_hw[id].bus, s_hw[id].mask);
    }
}

void ch32h4_timer_reset(uint8_t id) {
    if (valid(id)) {
        ch32h4_block_reset(s_hw[id].bus, s_hw[id].mask);
    }
}

uint32_t ch32h4_timer_input_clock(uint8_t id) {
    (void)id;
    /* Every timer on this part divides HCLK. There is no APB prescaler and no
     * x2 rule to apply -- RCC_ClocksTypeDef has no PCLK field at all. */
    return ch32h4_hclk();
}
