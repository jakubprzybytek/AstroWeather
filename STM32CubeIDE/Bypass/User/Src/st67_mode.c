#include "st67_mode.h"

#include "main.h"

#define ST67_ENABLE_PULSE_MS 20u
#define ST67_STARTUP_DELAY_MS 20u

void ST67_EnterMode(St67Mode mode)
{
  HAL_GPIO_WritePin(ST67_CHIP_EN_GPIO_Port, ST67_CHIP_EN_Pin, GPIO_PIN_RESET);
  HAL_GPIO_WritePin(ST67_CS_GPIO_Port, ST67_CS_Pin, GPIO_PIN_SET);

  if (mode == ST67_MODE_MANUFACTURE) {
    HAL_GPIO_WritePin(ST67_BOOT_GPIO_Port, ST67_BOOT_Pin, GPIO_PIN_RESET);
  } else {
    Error_Handler();
  }

  HAL_Delay(ST67_ENABLE_PULSE_MS);
  HAL_GPIO_WritePin(ST67_CHIP_EN_GPIO_Port, ST67_CHIP_EN_Pin, GPIO_PIN_SET);
  HAL_Delay(ST67_STARTUP_DELAY_MS);
}
