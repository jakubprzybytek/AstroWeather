#include <MainLoopTask.hpp>

#include <Astro/AstroDataRefreshTask.hpp>
#include <Display/Display.hpp>
#include <Display/LowBrightness.hpp>
#include <Debug/LogService.hpp>
#include <Settings/SettingsStore.hpp>
#include <Debug/PulseLed.hpp>

MainLoopTask& MainLoopTask::instance()
{
    static MainLoopTask task;
    return task;
}

MainLoopTask::MainLoopTask()
    : Task<2048>("MainLoopTask", osPriorityNormal)
{
}

void MainLoopTask::init(PulseLed& led, Settings::Store* settings, Display::Display* display)
{
    led_ = &led;
    settings_ = settings;
    display_ = display;
}

void MainLoopTask::run()
{
    // First, the local board's boot screens (about 3 s). Switch presses
    // meanwhile stay pending in the thread flags.
    if (display_ != nullptr)
    {
        display_->runBootScreens();
    }
    for (;;)
    {
        const uint32_t flags = osThreadFlagsWait(kEventSwitch1 | kEventSwitch2,
                                                 osFlagsWaitAny,
                                                 osWaitForever);
        if ((flags & osFlagsError) != 0U)
        {
            continue;
        }
        if ((flags & kEventSwitch1) != 0U)
        {
            LogService::instance().log(LogService::Level::Info,
                                       "MainLoopTask SWITCH_1 press");
            if (led_ != nullptr)
            {
                led_->pulse(250U);
            }
            HostController::AstroDataRefreshTask::instance().requestRefresh(
                HostController::RefreshTrigger::Switch1);
        }
        if ((flags & kEventSwitch2) != 0U)
        {
            LogService::instance().log(LogService::Level::Info,
                                       "MainLoopTask SWITCH_2 press");
            const bool low = LowBrightness::toggle();
            LogService::instance().logf(LogService::Level::Info,
                                        "Low brightness %s", low ? "on" : "off");
            // Saved like 'display low on|off', so the choice survives a reset.
            if (settings_ != nullptr)
            {
                settings_->setLowBrightness(low);
                const HAL_StatusTypeDef status = settings_->save();
                if (status != HAL_OK)
                {
                    LogService::instance().logf(LogService::Level::Error,
                                                "Settings save failed status=%u",
                                                static_cast<unsigned>(status));
                }
            }
            if (led_ != nullptr)
            {
                led_->pulse(50U);
            }
        }
    }
}