#pragma once

#include "main.h"

#include <cstdint>

class SCT2xxx
{
public:
    SCT2xxx(SPI_HandleTypeDef* spi,
            GPIO_TypeDef* enablePort,
            uint16_t enablePin,
            GPIO_TypeDef* latchPort,
            uint16_t latchPin);

    // Clocks data into the shift registers. The outputs do not change: LA/ stays
    // low, so the latches keep the previous data (SCT2024 truth table). Follow
    // with latch() to show it.
    HAL_StatusTypeDef shift(const uint8_t* data, uint16_t size);
    // Pulses LA/, moving the shifted data to the outputs.
    void latch();

    // shift() then latch().
    HAL_StatusTypeDef send(const uint8_t* data, uint16_t size);
    HAL_StatusTypeDef send(uint8_t value);

    // OE/: enable() turns all outputs on, disable() blanks them.
    void enable();
    void disable();

private:
    SPI_HandleTypeDef* spi_;
    GPIO_TypeDef* enablePort_;
    uint16_t enablePin_;
    GPIO_TypeDef* latchPort_;
    uint16_t latchPin_;
};