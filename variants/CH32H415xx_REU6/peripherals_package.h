/* Peripheral defaults for the CH32H415REU6. GENERATED -- see tools/genvariants.py.
 *
 * Every pin here is one this package bonds, with an alternate function this
 * part's own datasheet table confirms. A board includes this AFTER stating
 * what it wired differently; the #ifndef guards are what make that work.
 */
#pragma once

#ifndef PIN_SERIAL1_TX
#define PIN_SERIAL1_TX       PA9   /* USART1 TX, AF7 */
#endif
#ifndef PIN_SERIAL1_RX
#define PIN_SERIAL1_RX       PA10  /* USART1 RX, AF7 */
#endif
#ifndef PIN_WIRE_SCL
#define PIN_WIRE_SCL         PB6   /* I2C1, AF4 */
#endif
#ifndef PIN_WIRE_SDA
#define PIN_WIRE_SDA         PB7   /* I2C1, AF4 */
#endif
#ifndef PIN_SPI_SCK
#define PIN_SPI_SCK          PA5   /* SPI1, AF5 */
#endif
#ifndef PIN_SPI_MISO
#define PIN_SPI_MISO         PA6   /* SPI1, AF5 */
#endif
#ifndef PIN_SPI_MOSI
#define PIN_SPI_MOSI         PA7   /* SPI1, AF5 */
#endif
/* I2S1 is SPI2, which the datasheet calls I2S2. */
#ifndef PIN_I2S1_WS
#define PIN_I2S1_WS          PB12  /* AF5 */
#endif
#ifndef PIN_I2S1_AF_WS
#define PIN_I2S1_AF_WS       5     
#endif
#ifndef PIN_I2S1_CK
#define PIN_I2S1_CK          PB13  /* AF5 */
#endif
#ifndef PIN_I2S1_AF_CK
#define PIN_I2S1_AF_CK       5     
#endif
#ifndef PIN_I2S1_SD
#define PIN_I2S1_SD          PB15  /* AF5 */
#endif
#ifndef PIN_I2S1_AF_SD
#define PIN_I2S1_AF_SD       5     
#endif
/* I2S2 is SPI3, which the datasheet calls I2S3. */
#ifndef PIN_I2S2_WS
#define PIN_I2S2_WS          PA15  /* AF6 */
#endif
#ifndef PIN_I2S2_AF_WS
#define PIN_I2S2_AF_WS       6     
#endif
#ifndef PIN_I2S2_CK
#define PIN_I2S2_CK          PB3   /* AF6 */
#endif
#ifndef PIN_I2S2_AF_CK
#define PIN_I2S2_AF_CK       6     
#endif
#ifndef PIN_I2S2_SD
#define PIN_I2S2_SD          PA13  /* AF1 */
#endif
#ifndef PIN_I2S2_AF_SD
#define PIN_I2S2_AF_SD       1     
#endif

#ifndef SERIAL_PORT_MONITOR
#define SERIAL_PORT_MONITOR    Serial
#endif
#ifndef SERIAL_PORT_HARDWARE
#define SERIAL_PORT_HARDWARE   Serial1
#endif
