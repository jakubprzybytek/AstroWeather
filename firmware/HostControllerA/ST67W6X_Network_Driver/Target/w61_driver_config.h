/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file    w61_driver_config.h
  * @author  ST67 Application Team
  * @brief   Header file for the W61 configuration module
  ******************************************************************************
  * @attention
  *
  * Copyright (c) 2026 STMicroelectronics.
  * All rights reserved.
  *
  * This software is licensed under terms that can be found in the LICENSE file
  * in the root directory of this software component.
  * If no LICENSE file comes with this software, it is provided AS-IS.
  *
  ******************************************************************************
  */
/* USER CODE END Header */

/* Define to prevent recursive inclusion -------------------------------------*/
#ifndef W61_DRIVER_CONFIG_H
#define W61_DRIVER_CONFIG_H

#ifdef __cplusplus
extern "C" {
#endif /* __cplusplus */

/* Includes ------------------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported constants --------------------------------------------------------*/
/** ============================
  * AT Common
  * All available configuration defines in
  * Middlewares\ST\ST67W6X_Network_Driver\Driver\W61_at\w61_at_common.h
  * ============================
  */
/** Maximum SPI buffer size */
#define W61_MAX_SPI_XFER                        1520U

/** Enable/Disable System module logging */
#define SYS_LOG_ENABLE                          1

/** Debugging only: Enable/Disable AT log, i.e. logs the AT commands incoming/outcoming from/to the NCP */
#define W61_AT_LOG_ENABLE                       0
#include "logging.h"

/** Enable/Disable Modem command log */
#define MDM_CMD_LOG_ENABLE                      0

/* USER CODE BEGIN EC */
/* Project overrides of the driver's #ifndef defaults in spi_iface.c and
   w61_at_common.h. They must live here: this header is included ahead of those
   defaults, whereas definitions on the top-level CMake target never reach the
   driver, which is compiled in the generated STM32_Drivers library.

   Keep the driver's two own tasks just below DisplayRefresh (osPriorityRealtime,
   48). At their defaults of 53 and 54 they held off the display multiplexing
   for up to 14 ms during WiFi activity, seen as the whole display flashing.
   The modem RX task stays one above the SPI engine, as in the defaults. */
#define SPI_THREAD_PRIO                 46U
#define W61_MDM_RX_TASK_PRIO            47U

/* Larger SPI engine stack; see docs/CubeMXCompliance.md. */
#define SPI_THREAD_STACK_SIZE           1536U
/* USER CODE END EC */

#ifdef __cplusplus
}
#endif /* __cplusplus */

#endif /* W61_DRIVER_CONFIG_H */
