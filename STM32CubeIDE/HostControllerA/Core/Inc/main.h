/* USER CODE BEGIN Header */
/**
  ******************************************************************************
  * @file           : main.h
  * @brief          : Header for main.c file.
  *                   This file contains the common defines of the application.
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
#ifndef __MAIN_H
#define __MAIN_H

#ifdef __cplusplus
extern "C" {
#endif

/* Includes ------------------------------------------------------------------*/
#include "stm32g0xx_hal.h"

/* Private includes ----------------------------------------------------------*/
/* USER CODE BEGIN Includes */

/* USER CODE END Includes */

/* Exported types ------------------------------------------------------------*/
/* USER CODE BEGIN ET */

/* USER CODE END ET */

/* Exported constants --------------------------------------------------------*/
/* USER CODE BEGIN EC */

/* USER CODE END EC */

/* Exported macro ------------------------------------------------------------*/
/* USER CODE BEGIN EM */

/* USER CODE END EM */

/* Exported functions prototypes ---------------------------------------------*/
void Error_Handler(void);

/* USER CODE BEGIN EFP */

/* USER CODE END EFP */

/* Private defines -----------------------------------------------------------*/
#define LED_1_Pin GPIO_PIN_13
#define LED_1_GPIO_Port GPIOC
#define ST67_CHIP_EN_Pin GPIO_PIN_0
#define ST67_CHIP_EN_GPIO_Port GPIOA
#define ST67_SCK_Pin GPIO_PIN_1
#define ST67_SCK_GPIO_Port GPIOA
#define ST67_TX_Pin GPIO_PIN_2
#define ST67_TX_GPIO_Port GPIOA
#define ST67_RX_Pin GPIO_PIN_3
#define ST67_RX_GPIO_Port GPIOA
#define ST67_RDY_Pin GPIO_PIN_4
#define ST67_RDY_GPIO_Port GPIOA
#define ST67_RDY_EXTI_IRQn EXTI4_15_IRQn
#define ST67_CS_Pin GPIO_PIN_5
#define ST67_CS_GPIO_Port GPIOA
#define ST67_MISO_Pin GPIO_PIN_6
#define ST67_MISO_GPIO_Port GPIOA
#define ST67_MOSI_Pin GPIO_PIN_7
#define ST67_MOSI_GPIO_Port GPIOA
#define ST67_BOOT_Pin GPIO_PIN_1
#define ST67_BOOT_GPIO_Port GPIOB
#define ADDR_0_Pin GPIO_PIN_10
#define ADDR_0_GPIO_Port GPIOB
#define ADDR_1_Pin GPIO_PIN_11
#define ADDR_1_GPIO_Port GPIOB
#define SWITCH_1_Pin GPIO_PIN_12
#define SWITCH_1_GPIO_Port GPIOB
#define SWITCH_1_EXTI_IRQn EXTI4_15_IRQn
#define SWITCH_2_Pin GPIO_PIN_13
#define SWITCH_2_GPIO_Port GPIOB
#define SWITCH_2_EXTI_IRQn EXTI4_15_IRQn
#define ADDR_2_Pin GPIO_PIN_14
#define ADDR_2_GPIO_Port GPIOB
#define DISPLAY_2_Pin GPIO_PIN_15
#define DISPLAY_2_GPIO_Port GPIOA
#define DISPLAY_5_Pin GPIO_PIN_0
#define DISPLAY_5_GPIO_Port GPIOD
#define DISPLAY_3_Pin GPIO_PIN_1
#define DISPLAY_3_GPIO_Port GPIOD
#define DISPLAY_4_Pin GPIO_PIN_2
#define DISPLAY_4_GPIO_Port GPIOD
#define DISPLAY_1_Pin GPIO_PIN_3
#define DISPLAY_1_GPIO_Port GPIOD
#define SCT_SPI_SCK_Pin GPIO_PIN_3
#define SCT_SPI_SCK_GPIO_Port GPIOB
#define SCT_SPI_MOSI_Pin GPIO_PIN_5
#define SCT_SPI_MOSI_GPIO_Port GPIOB
#define SCT_LATCH_Pin GPIO_PIN_6
#define SCT_LATCH_GPIO_Port GPIOB
#define SCT_ENABLE_Pin GPIO_PIN_7
#define SCT_ENABLE_GPIO_Port GPIOB
#define LED_2_Pin GPIO_PIN_9
#define LED_2_GPIO_Port GPIOB

/* USER CODE BEGIN Private defines */

/* USER CODE END Private defines */

#ifdef __cplusplus
}
#endif

#endif /* __MAIN_H */
