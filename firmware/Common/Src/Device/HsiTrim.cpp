#include <Device/HsiTrim.hpp>

#include "main.h"

namespace HsiTrim {

uint8_t trim()
{
    return static_cast<uint8_t>((RCC->ICSCR & RCC_ICSCR_HSITRIM) >> RCC_ICSCR_HSITRIM_Pos);
}

uint8_t calibration()
{
    return static_cast<uint8_t>((RCC->ICSCR & RCC_ICSCR_HSICAL) >> RCC_ICSCR_HSICAL_Pos);
}

bool set(uint8_t value)
{
    if (value > kMax) {
        return false;
    }
    __HAL_RCC_HSI_CALIBRATIONVALUE_ADJUST(value);
    return true;
}

} // namespace HsiTrim
