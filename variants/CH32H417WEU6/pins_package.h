/* CH32H417WEU6 in the QFN68 package: the pins the silicon has.
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

/* GENERATED below this line -- see tools/genvariants.py. */

#define CH32H4_PACKAGE_ID   0x4172050D   /* the word at 0x1FFFF704, masked & ~0xF0 */
#define CH32H4_PINS_BONDED  50   /* of 96 pin numbers */

/* Analogue inputs, in the CH32H417QEU6's alias order so that
   A<n> means the same pad on every part. An alias whose pin
   this package does not bond is left undefined rather than
   reassigned. */
#define A0   PC0   /* ADC_IN10 */
#define A1   PC1   /* ADC_IN11 */
#define A2   PC2   /* ADC_IN12 */
#define A3   PC3   /* ADC_IN13 */
/* A4   PC4   is not bonded on this package */
/* A5   PC5   is not bonded on this package */
/* A6   PA0   is not bonded on this package */
/* A7   PA1   is not bonded on this package */
/* A8   PA2   is not bonded on this package */
/* A9   PA3   is not bonded on this package */
/* A10  PA4   is not bonded on this package */
#define A11  PA5   /* ADC_IN5 */
/* A12  PA6   is not bonded on this package */
/* A13  PA7   is not bonded on this package */
#define A14  PB0   /* ADC_IN8 */
#define A15  PB1   /* ADC_IN9 */
#define NUM_ANALOG_INPUTS    7

/* The 12-bit DACs. Fixed pads: there is no mux. */
/* No DAC1 on this part (resource table says '1 (DAC2)'). */
#define PIN_DAC2         PA5
#define DAC2             PIN_DAC2
#define PIN_DAC_OUT      PIN_DAC2

#define CH32H4_NUM_CAN       3
#define CH32H4_NUM_I2C       4
#define CH32H4_NUM_OPA       2
#define CH32H4_NUM_USART     7

/* The on-die ADC channels, which have no pad. */
#define ATEMP  (PINS_COUNT + 0)
#define AVREF  (PINS_COUNT + 1)
#define ADC_INTERNAL_TEMP_CHANNEL  16
#define ADC_INTERNAL_VREF_CHANNEL  17
#define digitalPinToInterrupt(p)  (p)

/* Which peripheral blocks this part has, from the
   datasheet's "Resource differences" table -- NOT from
   whether a signal appears on some pin, since the pin table
   lists functions generically and says so itself.

   A library for hardware the part lacks should #error on
   these rather than fail on a missing register. */
#define CH32H4_HAS_CMP      1
#define CH32H4_HAS_DFSDM    1
#define CH32H4_HAS_DVP      1
#define CH32H4_HAS_ETH      1
#define CH32H4_HAS_FSMC     1
#define CH32H4_HAS_GPHA     1
#define CH32H4_HAS_HSADC    1
#define CH32H4_HAS_I3C      1
#define CH32H4_HAS_LTDC     1
#define CH32H4_HAS_PIOC     1
#define CH32H4_HAS_RNG      1
#define CH32H4_HAS_SAI      1
#define CH32H4_HAS_SDMMC    1
#define CH32H4_HAS_SDRAM    1
#define CH32H4_HAS_SWPMI    1
#define CH32H4_HAS_UHSIF    1
#define CH32H4_HAS_USBHS    1
#define CH32H4_HAS_USBSS    1
#define CH32H4_HAS_QSPI2    1

/* Supply domains matter. Only PA5-PA7 and PE2-PE6 sit on the 3.3 V rail;
   every other pin is on VIO18 and idles well below 3.3 V, so a 3.3 V
   peripheral driven from one of them may not meet its input thresholds. */
#define PIN_IS_3V3_DOMAIN(p)     (((p) >= PA5 && (p) <= PA7) || ((p) >= PE2 && (p) <= PE6))
