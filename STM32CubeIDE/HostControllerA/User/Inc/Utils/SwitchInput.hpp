#pragma once

#include "cmsis_os2.h"

#include <cstdint>

namespace Utils {

class SwitchInput
{
public:
    static SwitchInput& instance();

    void attach(osThreadId_t recipient, uint32_t switch1Flag, uint32_t switch2Flag);
    void detach();
    static void handleExtiFalling(uint16_t gpioPin);

private:
    SwitchInput() = default;

    osThreadId_t recipient_ = nullptr;
    uint32_t switch1Flag_ = 0U;
    uint32_t switch2Flag_ = 0U;
};

} // namespace Utils
