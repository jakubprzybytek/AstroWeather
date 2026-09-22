#pragma once

#include "main.h"  // GPIO_TypeDef, HAL_GPIO_* types

class Led
{
public:
    Led(GPIO_TypeDef* port, uint16_t pin);

    void blink(uint32_t periodMs = 50);

private:
    GPIO_TypeDef* port_;
    uint16_t pin_;
};
