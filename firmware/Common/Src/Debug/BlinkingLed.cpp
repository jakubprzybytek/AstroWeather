#include <Debug/BlinkingLed.hpp>

BlinkingLed::BlinkingLed(GPIO_TypeDef *port, uint16_t pin, uint32_t onMs,
                         uint32_t offMs, const char *name)
    : Task<768>(name, osPriorityLow), port_(port), pin_(pin), onMs_(onMs),
      offMs_(offMs) {}

void BlinkingLed::init() {
}

void BlinkingLed::run() {
  for (;;) {
    HAL_GPIO_WritePin(port_, pin_, GPIO_PIN_SET);
    osDelay(onMs_);
    HAL_GPIO_WritePin(port_, pin_, GPIO_PIN_RESET);
    osDelay(offMs_);
  }
}
