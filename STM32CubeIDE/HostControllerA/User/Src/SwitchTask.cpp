#include <SwitchTask.hpp>

#include <Utils/Led.hpp>

#include "main.h"

// Defined in AstroWeather.cpp.
extern Led led2;

SwitchTask& SwitchTask::instance()
{
    static SwitchTask task;
    return task;
}

SwitchTask::SwitchTask()
    : Task<512>("SwitchTask", osPriorityNormal)
{}

void SwitchTask::onSwitch1Pressed()
{
    osThreadFlagsSet(getHandle(), kFlagSwitch1);
}

void SwitchTask::onSwitch2Pressed()
{
    osThreadFlagsSet(getHandle(), kFlagSwitch2);
}

void SwitchTask::run()
{
    for (;;)
    {
        uint32_t flags = osThreadFlagsWait(kFlagSwitch1 | kFlagSwitch2, osFlagsWaitAny, osWaitForever);
        if ((flags & osFlagsError) != 0U)
        {
            continue;
        }
        if ((flags & kFlagSwitch1) != 0U)
        {
            led2.blink(kSwitch1BlinkMs);
        }
        if ((flags & kFlagSwitch2) != 0U)
        {
            led2.blink(kSwitch2BlinkMs);
        }
    }
}

extern "C" void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
    if (GPIO_Pin == SWITCH_1_Pin)
    {
        SwitchTask::instance().onSwitch1Pressed();
    }
    else if (GPIO_Pin == SWITCH_2_Pin)
    {
        SwitchTask::instance().onSwitch2Pressed();
    }
}
