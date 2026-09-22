#include <HostController/MainLoopTask.hpp>

#include <HostController/AstroDataRefreshTask.hpp>
#include <Debug/LogService.hpp>
#include <St67HttpFetchTask.hpp>
#include <Utils/Led.hpp>

MainLoopTask& MainLoopTask::instance()
{
    static MainLoopTask task;
    return task;
}

MainLoopTask::MainLoopTask()
    : Task<1536>("MainLoopTask", osPriorityNormal)
{
}

void MainLoopTask::init(Led& led)
{
    led_ = &led;
}

void MainLoopTask::run()
{
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
                led_->blink(250U);
            }
            HostController::AstroDataRefreshTask::instance().requestRefresh(
                HostController::RefreshTrigger::Switch1);
        }
        if ((flags & kEventSwitch2) != 0U)
        {
            LogService::instance().log(LogService::Level::Info,
                                       "MainLoopTask SWITCH_2 press");
            if (led_ != nullptr)
            {
                led_->blink(50U);
            }
            HostController::TriggerSt67ConnectivityCycle();
        }
    }
}