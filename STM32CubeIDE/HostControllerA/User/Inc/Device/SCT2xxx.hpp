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

    HAL_StatusTypeDef send(const uint8_t* data, uint16_t size);
    HAL_StatusTypeDef send(uint8_t value);
    void enable();
    void disable();

private:
    SPI_HandleTypeDef* spi_;
    GPIO_TypeDef* enablePort_;
    uint16_t enablePin_;
    GPIO_TypeDef* latchPort_;
    uint16_t latchPin_;
};