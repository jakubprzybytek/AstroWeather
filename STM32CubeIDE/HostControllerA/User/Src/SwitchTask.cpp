#include <SwitchTask.hpp>

#include <Utils/Led.hpp>

#include <Debug/DebugService.hpp>

#include "main.h"

SwitchTask* SwitchTask::s_activeInstance = nullptr;
void (*SwitchTask::s_switch1Handler)() = nullptr;
void (*SwitchTask::s_switch2Handler)() = nullptr;

SwitchTask::SwitchTask(Led& led2)
    : Task<1536>("SwitchTask", osPriorityNormal), led2_(led2)
{
    s_activeInstance = this;
}

void SwitchTask::setSwitch1Handler(void (*handler)())
{
    s_switch1Handler = handler;
}

void SwitchTask::setSwitch2Handler(void (*handler)())
{
    s_switch2Handler = handler;
}

void SwitchTask::onSwitch1Pressed()
{
    DebugService::instance().logf(DebugService::Level::Info, "SWITCH_1 press");
    osThreadFlagsSet(getHandle(), kFlagSwitch1);
}

void SwitchTask::onSwitch2Pressed()
{
    DebugService::instance().logf(DebugService::Level::Info, "SWITCH_2 press");
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
            led2_.blink(kSwitch1BlinkMs);
            if (s_switch1Handler != nullptr)
            {
                s_switch1Handler();
            }
        }
        if ((flags & kFlagSwitch2) != 0U)
        {
            led2_.blink(kSwitch2BlinkMs);
            if (s_switch2Handler != nullptr)
            {
                s_switch2Handler();
            }
        }
    }
}

void SwitchTask::handleExtiFalling(uint16_t gpioPin)
{
    if (s_activeInstance == nullptr)
    {
        return;
    }
    if (gpioPin == SWITCH_1_Pin)
    {
        s_activeInstance->onSwitch1Pressed();
    }
    else if (gpioPin == SWITCH_2_Pin)
    {
        s_activeInstance->onSwitch2Pressed();
    }
}

extern "C" void HAL_GPIO_EXTI_Falling_Callback(uint16_t GPIO_Pin)
{
    SwitchTask::handleExtiFalling(GPIO_Pin);
}
