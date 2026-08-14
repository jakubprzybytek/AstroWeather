#include <Utils/Led.hpp>

#include "cmsis_os2.h"  // osDelay

Led::Led(GPIO_TypeDef* port, uint16_t pin)
    : port_(port), pin_(pin) {}

void Led::blink(uint32_t periodMs) {
  HAL_GPIO_TogglePin(port_, pin_);
  osDelay(periodMs);
  HAL_GPIO_TogglePin(port_, pin_);
}
