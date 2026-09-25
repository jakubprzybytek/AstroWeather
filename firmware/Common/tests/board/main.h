#pragma once

// Stand-in for a firmware project's Core/Inc/main.h, for the shared code's
// native tests: the pin labels the shared code uses, at the positions they have
// on the AstroWeather board in both CubeMX projects. The HAL comes from the
// stub in ../stubs.

#include "stm32g0xx_hal.h"

#define LED_1_Pin GPIO_PIN_13
#define LED_1_GPIO_Port GPIOC
#define ADDR_0_Pin GPIO_PIN_10
#define ADDR_0_GPIO_Port GPIOB
#define ADDR_1_Pin GPIO_PIN_11
#define ADDR_1_GPIO_Port GPIOB
#define SWITCH_1_Pin GPIO_PIN_12
#define SWITCH_1_GPIO_Port GPIOB
#define SWITCH_2_Pin GPIO_PIN_13
#define SWITCH_2_GPIO_Port GPIOB
#define ADDR_2_Pin GPIO_PIN_14
#define ADDR_2_GPIO_Port GPIOB
#define DISPLAY_2_EN_Pin GPIO_PIN_15
#define DISPLAY_2_EN_GPIO_Port GPIOA
#define DISPLAY_5_EN_Pin GPIO_PIN_0
#define DISPLAY_5_EN_GPIO_Port GPIOD
#define DISPLAY_3_EN_Pin GPIO_PIN_1
#define DISPLAY_3_EN_GPIO_Port GPIOD
#define DISPLAY_4_EN_Pin GPIO_PIN_2
#define DISPLAY_4_EN_GPIO_Port GPIOD
#define DISPLAY_1_EN_Pin GPIO_PIN_3
#define DISPLAY_1_EN_GPIO_Port GPIOD
#define SCT_LATCH_Pin GPIO_PIN_6
#define SCT_LATCH_GPIO_Port GPIOB
#define SCT_ENABLE_Pin GPIO_PIN_7
#define SCT_ENABLE_GPIO_Port GPIOB
#define LED_2_Pin GPIO_PIN_9
#define LED_2_GPIO_Port GPIOB
