#pragma once

// Test-side controls for the HAL and RTOS stubs: a fake millisecond clock
// shared by HAL_GetTick() and osKernelGetTickCount(), and a GPIO model where
// each input pin is strapped low, high or left floating (then it follows the
// pull configured by HAL_GPIO_Init()).

#include "stm32g0xx_hal.h"

#include <cstdint>

namespace Stub {

enum class Strap : uint8_t { Floating, Low, High };

void reset();

void setTick(uint32_t tickMs);
void advanceTick(uint32_t deltaMs);
uint32_t tick();

void setStrap(GPIO_TypeDef* port, uint16_t pin, Strap strap);
GPIO_PinState outputState(GPIO_TypeDef* port, uint16_t pin);
uint32_t configuredPull(GPIO_TypeDef* port, uint16_t pin);
uint32_t configuredMode(GPIO_TypeDef* port, uint16_t pin);

} // namespace Stub
