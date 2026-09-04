/* CH32H417xx QEU6: the default pin for each peripheral.
 *
 * Every definition here is guarded, and that is the whole point of the file
 * being separate from pins_package.h. A board includes pins_package.h first
 * to get the pin names, defines whatever it wires differently, and includes
 * this last -- so a board that puts SPI1 somewhere else says so in one line
 * and inherits the rest.
 *
 * These are defaults, not facts about the silicon. Each names a pin the mux
 * can actually carry that signal on -- see pin_map_package.c, which is the
 * silicon's own table and the thing the libraries search -- but the choice
 * among the options is a convention this package offers, and a board is free
 * to disagree with any of it.
 */
#pragma once

#include "pins_package.h"

/* Peripheral pins, proven on this board. */
#ifndef PIN_SERIAL1_TX
#define PIN_SERIAL1_TX   PA9    /* AF7 */
#endif
#ifndef PIN_SERIAL1_RX
#define PIN_SERIAL1_RX   PA10   /* AF7 */
#endif
#ifndef PIN_WIRE_SDA
#define PIN_WIRE_SDA     PB7    /* I2C1, AF4 */
#endif
#ifndef PIN_WIRE_SCL
#define PIN_WIRE_SCL     PB6    /* I2C1, AF4 */
#endif
#ifndef PIN_SPI_SCK
#define PIN_SPI_SCK      PA5    /* SPI1, AF5 */
#endif
#ifndef PIN_SPI_MISO
#define PIN_SPI_MISO     PA6    /* SPI1, AF5 */
#endif
#ifndef PIN_SPI_MOSI
#define PIN_SPI_MOSI     PA7    /* SPI1, AF5 */
#endif

/* A second SPI, for the slave side. SPI4's trio is the only other one on the
   3.3 V rail, so a master on SPI1 can talk to it without either end sitting
   below the other's input threshold, and PE3/PE4 are the last two free pins
   on that rail -- PE3 for a master driving chip select, PE4 for the slave's
   NSS. See g_spi_nss_map in pin_map.c for why NSS is the one signal with no
   published table behind it. */
#ifndef PIN_SPI_SLAVE_SCK
#define PIN_SPI_SLAVE_SCK   PE2    /* SPI4, AF5 */
#endif
#ifndef PIN_SPI_SLAVE_MISO
#define PIN_SPI_SLAVE_MISO  PE5    /* SPI4, AF5 */
#endif
#ifndef PIN_SPI_SLAVE_MOSI
#define PIN_SPI_SLAVE_MOSI  PE6    /* SPI4, AF5 */
#endif
#ifndef PIN_SPI_SLAVE_CS
#define PIN_SPI_SLAVE_CS    PE4    /* SPI4 NSS, AF5 */
#endif

/* A second I2C, for the slave side. I2C is multi-drop, so this shares the
   bus the display is already on rather than needing one of its own: wire PB6
   to PD12 and PB7 to PD13 and the display's pull-ups serve all three
   devices. PD13 is otherwise unused and PD12 only appears as an SDMMC
   alternate this board does not take. */
#ifndef PIN_WIRE_SLAVE_SCL
#define PIN_WIRE_SLAVE_SCL  PD12   /* I2C4, AF4 */
#endif
#ifndef PIN_WIRE_SLAVE_SDA
#define PIN_WIRE_SLAVE_SDA  PD13   /* I2C4, AF4 */
#endif

/* I2S.
 *
 * WCH numbers these in a way that catches everyone once: there are TWO I2S
 * blocks, and they are the audio halves of SPI2 and SPI3. The datasheet calls
 * them I2S1 and I2S2 -- so I2S1 lives in the SPI2 registers and I2S2 lives in
 * the SPI3 registers, and there is no I2S3 at all. Names here follow the
 * datasheet; the code that drives them names the SPI block, which is what the
 * register map, the RCC gate and the DMA request are all keyed on.
 *
 * The alternate function is per PIN, not per peripheral: I2S2's clock is AF6
 * on PB3 while its data is AF7 on PB2. Configuring all three with one AF works
 * for I2S1 by luck -- every one of its pins is AF5 -- and silently fails for
 * I2S2.
 *
 * I2S1 is the one wired to the amplifier on this board. I2S2's pins are the
 * free choice among those the mux offers: PA4 is the DAC output and PB5 is the
 * OneWire pin, so WS goes to PA15 and data to PB2.
 */
#ifndef PIN_I2S1_WS
#define PIN_I2S1_WS      PB12   /* I2S1 = SPI2, on HB1 */
#endif
#ifndef PIN_I2S1_CK
#define PIN_I2S1_CK      PB13
#endif
#ifndef PIN_I2S1_SD
#define PIN_I2S1_SD      PB15
#endif
#ifndef PIN_I2S1_AF_WS
#define PIN_I2S1_AF_WS   5
#endif
#ifndef PIN_I2S1_AF_CK
#define PIN_I2S1_AF_CK   5
#endif
#ifndef PIN_I2S1_AF_SD
#define PIN_I2S1_AF_SD   5
#endif

#ifndef PIN_I2S2_WS
#define PIN_I2S2_WS      PA15   /* I2S2 = SPI3, on HB1 */
#endif
#ifndef PIN_I2S2_CK
#define PIN_I2S2_CK      PB3
#endif
#ifndef PIN_I2S2_SD
#define PIN_I2S2_SD      PB2
#endif
#ifndef PIN_I2S2_AF_WS
#define PIN_I2S2_AF_WS   6
#endif
#ifndef PIN_I2S2_AF_CK
#define PIN_I2S2_AF_CK   6
#endif
#ifndef PIN_I2S2_AF_SD
#define PIN_I2S2_AF_SD   7
#endif

/* The default instance, for sketches that never name one. */
#ifndef PIN_I2S_WS
#define PIN_I2S_WS       PIN_I2S1_WS
#endif
#ifndef PIN_I2S_CK
#define PIN_I2S_CK       PIN_I2S1_CK
#endif
#ifndef PIN_I2S_SD
#define PIN_I2S_SD       PIN_I2S1_SD
#endif
