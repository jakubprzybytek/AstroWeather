#include <Utils/SwitchInput.hpp>

#include "main.h"

namespace Utils {

SwitchInput& SwitchInput::instance()
{
    static SwitchInput input;
    return input;
}

void SwitchInput::attach(osThreadId_t recipient, uint32_t switch1Flag,
                         uint32_t switch2Flag)
{
    recipient_ = recipient;
    switch1Flag_ = switch1Flag;
    switch2Flag_ = switch2Flag;
}

void SwitchInput::detach()
{
    recipient_ = nullptr;
    switch1Flag_ = 0U;
    switch2Flag_ = 0U;
}

void SwitchInput::handleExtiFalling(uint16_t gpioPin)
{
    SwitchInput& input = instance();
    if (input.recipient_ == nullptr)
    {
        return;
    }

    uint32_t flag = 0U;
    if (gpioPin == SWITCH_1_Pin)
    {
        flag = input.switch1Flag_;
    }
    else if (gpioPin == SWITCH_2_Pin)
    {
        flag = input.switch2Flag_;
    }
    if (flag != 0U)
    {
        osThreadFlagsSet(input.recipient_, flag);
    }
}

} // namespace Utils

extern "C" void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
    Utils::SwitchInput::handleExtiFalling(GPIO_Pin);
}
