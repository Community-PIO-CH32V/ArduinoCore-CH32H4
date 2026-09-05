/* CH32H417QEU6-R0-1v1, the WCH evaluation board.
 *
 * Everything the SILICON decides is in the package base next door; this file
 * is only what THIS BOARD wired. A second board on the same chip copies this
 * file, changes the handful of lines below, and shares the rest.
 *
 * The order matters. pins_package.h comes first so the pin names exist, then
 * this board states what it wired differently, then peripherals_package.h
 * fills in every peripheral default this board did not override -- each of
 * those is guarded with #ifndef, so whatever is defined above wins.
 */
#pragma once

#include "../CH32H417xx_QEU6/pins_package.h"

#define PIN_ONEWIRE      PB5    /* open-drain; needs a REAL external pull-up */
#define PIN_SDMMC_CK     PC12
#define PIN_SDMMC_CMD    PD2
#define PIN_SDMMC_D0     PC8
#define PIN_ETH_LED_LINK PF0    /* driven by the PHY itself, AF10 */
#define PIN_ETH_LED_ACT  PF2

/* The two user LEDs.
 *
 * NOT CONNECTED OUT OF THE BOX. The LEDs are fitted, but neither is wired to
 * the microcontroller: each sits behind a header pin that has to be jumpered
 * to the pin below it. LED1 is under PE2 and LED2 is under PE3, which is why
 * these are the two.
 *
 * So digitalWrite(LED_BUILTIN, HIGH) on an untouched board does exactly what
 * it says and lights nothing. That is worth knowing before spending an
 * evening on it.
 *
 * PE2-PE6 are on the 3.3 V rail, unlike most of this part, so an LED driven
 * from here gets a real 3.3 V swing rather than VIO18's 1.8 V.
 *
 * BOTH PINS HAVE ANOTHER JOB by default. PE2 is SPI4's clock -- the slave-mode
 * SPI, PIN_SPI_SLAVE_SCK below -- and PE3 is what the SPI-slave test uses as
 * the master's chip select. A board with the LED jumpers on cannot also run
 * SPI4 as a slave on the default pins. Nothing enforces that; it is one pin
 * driving two things, and the LED wins whichever was configured last. */
#define PIN_LED1         PE2
#define PIN_LED2         PE3
#define LED_BUILTIN      PIN_LED1

/* The board's two test jumpers, both there so a test can prove a wire rather
   than a register: PA6-PA7 is the SPI1 loopback, PC3-PC4 crosses the
   VIO18/VDDIO domain boundary. */
#define PIN_LOOPBACK_A   PA6
#define PIN_LOOPBACK_B   PA7
#define PIN_JUMPER_A     PC3
#define PIN_JUMPER_B     PC4

/* Which I2S the amplifier is on. Board wiring, not silicon: both blocks
   exist on every QEU6 part, and this board brought I2S1 out to the amp. */
#define PIN_I2S_WS       PIN_I2S1_WS
#define PIN_I2S_CK       PIN_I2S1_CK
#define PIN_I2S_SD       PIN_I2S1_SD

#include "../CH32H417xx_QEU6/peripherals_package.h"

#define SERIAL_PORT_HARDWARE   Serial1
#define SERIAL_PORT_MONITOR    Serial
