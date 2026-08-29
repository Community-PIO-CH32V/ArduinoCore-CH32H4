#include "Arduino.h"

void pinMode(pin_size_t pin, PinMode mode) {
    if (pin >= PINS_COUNT) {
        return;
    }
    const ch32h4_pin_t *p = &g_pins[pin];

    uint8_t cfg;
    switch (mode) {
        case OUTPUT:
            cfg = CH32H4_CFG_OUT_PP_50;
            break;
        case INPUT_PULLUP:
        case INPUT_PULLDOWN:
            cfg = CH32H4_CFG_IN_PUPD;
            break;
        case OUTPUT_OPENDRAIN:
            /* No internal pull-up is available in this mode: the F1-style
             * encoding does not offer one. A 1-Wire or I2C line needs a real
             * resistor, and a driver should report a missing resistor as a
             * missing resistor rather than as a missing device. */
            cfg = CH32H4_CFG_OUT_OD_50;
            break;
        case INPUT:
        default:
            cfg = CH32H4_CFG_IN_FLOATING;
            break;
    }

    ch32h4_pin_af(p->port, p->bit, CH32H4_AF_NONE, cfg);

    /* In the pull-up/pull-down encoding it is OUTDR that picks the direction. */
    if (mode == INPUT_PULLUP) {
        p->port->BSHR = (1u << p->bit);
    } else if (mode == INPUT_PULLDOWN) {
        p->port->BCR = (1u << p->bit);
    }
}

/* In ITCM: this is the hottest thing in the core, and it is the one place
 * where a bit-banged protocol's timing is decided. */
__itcm_func void digitalWrite(pin_size_t pin, PinStatus val) {
    if (pin >= PINS_COUNT) {
        return;
    }
    const ch32h4_pin_t *p = &g_pins[pin];
    if (val == LOW) {
        p->port->BCR = (1u << p->bit);
    } else {
        p->port->BSHR = (1u << p->bit);
    }
}

__itcm_func PinStatus digitalRead(pin_size_t pin) {
    if (pin >= PINS_COUNT) {
        return LOW;
    }
    const ch32h4_pin_t *p = &g_pins[pin];
    return (p->port->INDR & (1u << p->bit)) ? HIGH : LOW;
}
