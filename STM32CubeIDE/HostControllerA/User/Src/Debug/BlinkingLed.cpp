#include <bits/stdc++.h>

#include <Debug/BlinkingLed.hpp>

BlinkingLed::BlinkingLed(GPIO_TypeDef *port, uint16_t pin, uint32_t periodMs,
                         const char *name)
    : Task<256>(name, osPriorityLow), port_(port), pin_(pin),
      periodMs_(periodMs) {}

void BlinkingLed::init() {
}

void BlinkingLed::run() {
  for (;;) {
    HAL_GPIO_TogglePin(port_, pin_);
    osDelay(rand() % periodMs_);
  }
}
