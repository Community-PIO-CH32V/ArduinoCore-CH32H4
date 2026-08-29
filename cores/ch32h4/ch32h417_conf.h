/* Core-owned SDK configuration.
 *
 * Upstream ships this per-example under <example>/<core>/User/ rather than in
 * EVT/EXAM/SRC, because it selects which peripheral headers get pulled in --
 * project configuration rather than library code. That is why
 * system/ch32h417lib does not carry it and this core does.
 *
 * One deliberate difference from the vendor's copy: it does NOT include
 * debug.h. That header retargets _write() to a UART of its own, which collides
 * with the core's console and with Print, and it also defines the
 * Run_Core_V3F / Run_Core_V5F macros this core replaces with its own
 * single-ELF wake. The MicroPython port omits it for the same reason.
 */
/********************************** (C) COPYRIGHT *******************************
* File Name          : ch32h417_conf.h
* Author             : WCH
* Version            : V1.0.0
* Date               : 2025/03/01
* Description        : Library configuration file.
*********************************************************************************
* Copyright (c) 2025 Nanjing Qinheng Microelectronics Co., Ltd.
* Attention: This software (modified or not) and binary are used for
* microcontroller manufactured by Nanjing Qinheng Microelectronics.
*******************************************************************************/
#ifndef __CH32H417_CONF_H
#define __CH32H417_CONF_H

#include "ch32h417_adc.h"
#include "ch32h417_can.h"
#include "ch32h417_crc.h"
#include "ch32h417_dac.h"
#include "ch32h417_dbgmcu.h"
#include "ch32h417_dfsdm.h"
#include "ch32h417_dma.h"
#include "ch32h417_dvp.h"
#include "ch32h417_ecdc.h"
#include "ch32h417_eth.h"
#include "ch32h417_exti.h"
#include "ch32h417_flash.h"
#include "ch32h417_fmc.h"
#include "ch32h417_gpha.h"
#include "ch32h417_gpio.h"
#include "ch32h417_hsadc.h"
#include "ch32h417_hsem.h"
#include "ch32h417_i2c.h"
#include "ch32h417_i3c.h"
#include "ch32h417_ipc.h"
#include "ch32h417_iwdg.h"
#include "ch32h417_lptim.h"
#include "ch32h417_ltdc.h"
#include "ch32h417_opa.h"
#include "ch32h417_pwr.h"
#include "ch32h417_qspi.h"
#include "ch32h417_rcc.h"
#include "ch32h417_rng.h"
#include "ch32h417_rtc.h"
#include "ch32h417_sai.h"
#include "ch32h417_sdio.h"
#include "ch32h417_sdmmc.h"
#include "ch32h417_serdes.h"
#include "ch32h417_spi.h"
#include "ch32h417_swpmi.h"
#include "ch32h417_tim.h"
#include "ch32h417_usart.h"
#include "ch32h417_wwdg.h"

#endif /* __CH32H417_CONF_H */
