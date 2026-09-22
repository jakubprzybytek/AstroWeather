#pragma once

#include "main.h"

#include <cstdint>

namespace Display {

uint8_t detectBoardId(GPIO_TypeDef* port0, uint16_t pin0,
                      GPIO_TypeDef* port1, uint16_t pin1,
                      GPIO_TypeDef* port2, uint16_t pin2);
uint16_t boardAddress(uint8_t boardId);
uint16_t detectBoardAddress();

} // namespace Display
