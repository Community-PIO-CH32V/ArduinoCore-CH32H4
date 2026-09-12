/* CH32H417xx in the QEU6 package: the pins the silicon has.
 *
 * This file is a PACKAGE base, not a board. Everything in it is decided by
 * the chip and its package -- how the pins are numbered, which pads reach the
 * ADC, which supply rail each pad sits on -- so every board built on a
 * CH32H417xx QEU6 shares it unchanged. What a particular PCB decided goes in
 * that board's own pins_arduino.h, which includes this and then
 * peripherals_package.h.
 *
 * Pin numbers are dense and port-ordered: port index * 16 + bit. PA0..PA15 are
 * 0..15, PB 16..31, PC 32..47, PD 48..63, PE 64..79, PF 80..95. That is the
 * same encoding the MicroPython port uses for this silicon, so its peripheral
 * tables port across unchanged.
 *
 * The alternate-function maps in pin_map_package.c are the silicon's, and are
 * what analogWrite() and the peripheral libraries search.
 */
#pragma once

#include <stdint.h>

#define PINS_COUNT           96
#define NUM_DIGITAL_PINS     PINS_COUNT
#define NUM_ANALOG_INPUTS    16

#define PA0   0
#define PA1   1
#define PA2   2
#define PA3   3
#define PA4   4
#define PA5   5
#define PA6   6
#define PA7   7
#define PA8   8
#define PA9   9
#define PA10  10
#define PA11  11
#define PA12  12
#define PA13  13
#define PA14  14
#define PA15  15

#define PB0   16
#define PB1   17
#define PB2   18
#define PB3   19
#define PB4   20
#define PB5   21
#define PB6   22
#define PB7   23
#define PB8   24
#define PB9   25
#define PB10  26
#define PB11  27
#define PB12  28
#define PB13  29
#define PB14  30
#define PB15  31

#define PC0   32
#define PC1   33
#define PC2   34
#define PC3   35
#define PC4   36
#define PC5   37
#define PC6   38
#define PC7   39
#define PC8   40
#define PC9   41
#define PC10  42
#define PC11  43
#define PC12  44
#define PC13  45
#define PC14  46
#define PC15  47

#define PD0   48
#define PD1   49
#define PD2   50
#define PD3   51
#define PD4   52
#define PD5   53
#define PD6   54
#define PD7   55
#define PD8   56
#define PD9   57
#define PD10  58
#define PD11  59
#define PD12  60
#define PD13  61
#define PD14  62
#define PD15  63

#define PE0   64
#define PE1   65
#define PE2   66
#define PE3   67
#define PE4   68
#define PE5   69
#define PE6   70
#define PE7   71
#define PE8   72
#define PE9   73
#define PE10  74
#define PE11  75
#define PE12  76
#define PE13  77
#define PE14  78
#define PE15  79

#define PF0   80
#define PF1   81
#define PF2   82
#define PF3   83
#define PF4   84
#define PF5   85
#define PF6   86
#define PF7   87
#define PF8   88
#define PF9   89
#define PF10  90
#define PF11  91
#define PF12  92
#define PF13  93
#define PF14  94
#define PF15  95

/* Analog inputs. PA0-PA3 are ADC1 channels 0-3 (and also TIM2 CH1-4 on AF1);
   PC0-PC5 are ADC1 channels 10-15. Nothing else on this package reaches the
   ADC. */
#define A0   PC0
#define A1   PC1
#define A2   PC2
#define A3   PC3
#define A4   PC4
#define A5   PC5
#define A6   PA0
#define A7   PA1
#define A8   PA2
#define A9   PA3
/* PA4-PA7 and PB0-PB1 are ADC channels 4-9. They are appended rather than
   renumbered into place: A0..A9 are what existing sketches and tests already
   name, and moving them to make the numbering tidy would silently change which
   pad a sketch reads. Several carry a board function as well -- PA4 is DAC1,
   PA5 is DAC2 and the SPI clock, PA6/PA7 are the SPI data pins and the
   loopback jumper -- which is board wiring, not silicon, so the channels are
   declared and the choice is left to the sketch. */
#define A10  PA4
#define A11  PA5
#define A12  PA6
#define A13  PA7
#define A14  PB0
#define A15  PB1

/* The two internal ADC inputs, as pin numbers.
 *
 * They are not pins -- neither has a pad, a port or a bit, and neither can be
 * configured, read digitally or given an interrupt. They are numbered above
 * PINS_COUNT precisely so that any code treating them as real pins indexes
 * past the end of g_pins and fails loudly rather than reading pin 0.
 *
 * Numbering them at all is what lets analogRead(ATEMP) and
 * ADCInput(A0, ATEMP) work without a second API: everything that takes a pin
 * goes through ch32h4_adc_channel(), which knows about these two and returns
 * 0xFF for anything else with no ADC input.
 *
 *   ATEMP -- ADC1_IN16, the on-die temperature sensor. analogReadTemp()
 *            converts it to degrees Celsius using the factory calibration.
 *   AVREF -- ADC1_IN17, the internal reference, nominally 1.20 V. Reading it
 *            is the check on the assumption that VDDA is 3.3 V; see
 *            ch32h4_vdda_volts().
 *
 * Both need a long sample window -- they are driven through a high impedance,
 * and a short one reads the sample-and-hold's previous contents instead. The
 * core picks the slowest setting for them automatically.
 */
#define ATEMP  (PINS_COUNT + 0)
#define AVREF  (PINS_COUNT + 1)

#define ADC_INTERNAL_TEMP_CHANNEL  16
#define ADC_INTERNAL_VREF_CHANNEL  17

/* An interrupt "number" here is just the pin number. Note that EXTI lines are
   shared by pin NUMBER across ports, so PA0 and PB0 cannot both have one --
   attachInterrupt refuses the second rather than stealing the line. */
#define digitalPinToInterrupt(p)  (p)

/* The two 12-bit DACs. These pins are fixed -- there is no mux, and neither
   channel can come out anywhere else.

   Both are also ADC inputs (PA4 is ADC4, PA5 is ADC5), which is what makes the
   DAC verifiable with nothing wired to the board: write a code, read the same
   pad back with the ADC.

   PA5 is the SPI1 clock as well. See the note on analogWrite() about what it
   means for a pin to be DAC-capable. */
#define PIN_DAC1         PA4
#define PIN_DAC2         PA5
#define DAC1             PIN_DAC1
#define DAC2             PIN_DAC2
#define PIN_DAC_OUT      PIN_DAC1   /* the older name for DAC1 */

/* Supply domains matter. Only PA5-PA7 and PE2-PE6 sit on the 3.3 V rail; every
   other pin is on VIO18 and idles well below 3.3 V, so a 3.3 V peripheral
   driven from one of them may not meet its input thresholds. */
#define PIN_IS_3V3_DOMAIN(p)     (((p) >= PA5 && (p) <= PA7) || ((p) >= PE2 && (p) <= PE6))
