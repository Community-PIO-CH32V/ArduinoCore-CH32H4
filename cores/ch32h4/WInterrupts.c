#include "Arduino.h"
#include "ch32h4_irq.h"

/* EXTI lines are shared by pin NUMBER across every port: PA0, PB0, PC0 and PD0
 * all map to line 0, and only one of them can have an interrupt at a time.
 *
 * The second request is REFUSED and recorded, not granted. Granting it would
 * silently steal the line from a driver that is currently working, and the
 * victim would fail somewhere else entirely -- the far more expensive failure
 * of the two. A refusal is at least visible to the caller through
 * ch32h4_exti_owner().
 */
static voidFuncPtr      s_handlers[16];
static voidFuncPtrParam s_handlers_param[16];
static void            *s_params[16];
static int8_t           s_owner[16] = {
    -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1, -1
};

int ch32h4_exti_owner(uint8_t line) {
    return (line < 16) ? s_owner[line] : -1;
}

static uint8_t port_source(GPIO_TypeDef *port) {
    if (port == GPIOA) return GPIO_PortSourceGPIOA;
    if (port == GPIOB) return GPIO_PortSourceGPIOB;
    if (port == GPIOC) return GPIO_PortSourceGPIOC;
    if (port == GPIOD) return GPIO_PortSourceGPIOD;
    if (port == GPIOE) return GPIO_PortSourceGPIOE;
    return GPIO_PortSourceGPIOF;
}

static void exti_configure(pin_size_t pin, PinStatus mode) {
    const ch32h4_pin_t *p = &g_pins[pin];
    const uint8_t line = p->bit;

    /* The AF mux is not involved, but AFIO's clock gates the EXTI line
     * selection register, and without it the write below is discarded. */
    ch32h4_clock_enable(CH32_BUS_HB2, RCC_HB2Periph_AFIO);
    GPIO_EXTILineConfig(port_source(p->port), line);

    EXTI_InitTypeDef e = {0};
    e.EXTI_Line = (1u << line);
    e.EXTI_Mode = EXTI_Mode_Interrupt;
    switch (mode) {
        case RISING:  e.EXTI_Trigger = EXTI_Trigger_Rising; break;
        case FALLING: e.EXTI_Trigger = EXTI_Trigger_Falling; break;
        default:      e.EXTI_Trigger = EXTI_Trigger_Rising_Falling; break;
    }
    e.EXTI_LineCmd = ENABLE;
    EXTI_Init(&e);

    NVIC_EnableIRQ((line < 8) ? EXTI7_0_IRQn : EXTI15_8_IRQn);
}

void attachInterrupt(pin_size_t pin, voidFuncPtr callback, PinStatus mode) {
    if (pin >= PINS_COUNT || callback == NULL) {
        return;
    }
    const uint8_t line = g_pins[pin].bit;

    if (s_owner[line] >= 0 && s_owner[line] != (int8_t)pin) {
        return;   /* refused: another port's pin N already owns line N */
    }

    s_handlers[line] = callback;
    s_handlers_param[line] = NULL;
    s_params[line] = NULL;
    s_owner[line] = (int8_t)pin;
    exti_configure(pin, mode);
}

void attachInterruptParam(pin_size_t pin, voidFuncPtrParam callback,
                          PinStatus mode, void *param) {
    if (pin >= PINS_COUNT || callback == NULL) {
        return;
    }
    const uint8_t line = g_pins[pin].bit;

    if (s_owner[line] >= 0 && s_owner[line] != (int8_t)pin) {
        return;
    }

    s_handlers[line] = NULL;
    s_handlers_param[line] = callback;
    s_params[line] = param;
    s_owner[line] = (int8_t)pin;
    exti_configure(pin, mode);
}

void detachInterrupt(pin_size_t pin) {
    if (pin >= PINS_COUNT) {
        return;
    }
    const uint8_t line = g_pins[pin].bit;
    if (s_owner[line] != (int8_t)pin) {
        return;   /* not ours to detach */
    }
    EXTI->INTENR &= ~(1u << line);
    s_handlers[line] = NULL;
    s_handlers_param[line] = NULL;
    s_params[line] = NULL;
    s_owner[line] = -1;
}

__itcm_func static void exti_dispatch(uint8_t lo, uint8_t hi) {
    for (uint8_t l = lo; l <= hi; l++) {
        const uint32_t bit = (1u << l);
        if (EXTI->INTFR & bit) {
            /* Acknowledge every latched bit, not only the ones acted on, or
             * the handler re-enters forever. Acknowledge BEFORE calling the
             * callback, so an edge arriving during it is not lost. */
            EXTI->INTFR = bit;
            if (s_handlers[l]) {
                s_handlers[l]();
            } else if (s_handlers_param[l]) {
                s_handlers_param[l](s_params[l]);
            }
        }
    }
}

/* Attributes on the declarations -- see ch32h4_irq.h. */
void CH32H4_IRQ_HANDLER(EXTI7_0_IRQHandler);
void CH32H4_IRQ_HANDLER(EXTI15_8_IRQHandler);

void EXTI7_0_IRQHandler(void) {
    exti_dispatch(0, 7);
}

void EXTI15_8_IRQHandler(void) {
    exti_dispatch(8, 15);
}
