// #include <Debug/BlinkingLed.hpp>
#include <Debug/DebugService.hpp>

BlinkingLed::BlinkingLed(GPIO_TypeDef *port, uint16_t pin, uint32_t periodMs,
                         const char *name)
    : Task<768>(name, osPriorityLow), port_(port), pin_(pin),
      periodMs_(periodMs) {}

void BlinkingLed::init() {
}

void BlinkingLed::run() {
  for (;;) {
    HAL_GPIO_TogglePin(port_, pin_);
    // DebugService::instance().logf(DebugService::Level::Info, "%s toggled", getName());
    osDelay(periodMs_);
  }
}
