#include <HostController/ClockTask.hpp>

#include <Debug/LogService.hpp>
#include <Display/Display.hpp>

#include "main.h"

namespace {

constexpr uint32_t kRetryDelayMs = 1000U;
constexpr uint32_t kSecondsPerMinute = 60U;
}  // namespace

extern "C" RTC_HandleTypeDef hrtc;

ClockTask& ClockTask::instance()
{
    static ClockTask task;
    return task;
}

ClockTask::ClockTask()
    : Task<1024>("Clock", osPriorityBelowNormal)
{
}

void ClockTask::setDisplay(Display::Display* display)
{
    display_ = display;
}

void ClockTask::run()
{
    // 0xFF never matches a real minute, so the first read always draws.
    uint8_t shownMinute = 0xFFU;

    for (;;)
    {
        RTC_TimeTypeDef time = {};
        RTC_DateTypeDef date = {};
        // GetDate must follow GetTime: it unlocks the shadow registers that
        // GetTime froze, otherwise the time stops advancing.
        const bool valid = HAL_RTC_GetTime(&hrtc, &time, RTC_FORMAT_BIN) == HAL_OK &&
                           HAL_RTC_GetDate(&hrtc, &date, RTC_FORMAT_BIN) == HAL_OK;
        if (!valid)
        {
            LogService::instance().log(LogService::Level::Error,
                                       "Clock RTC read failed");
            osDelay(kRetryDelayMs);
            continue;
        }

        if (time.Minutes != shownMinute && display_ != nullptr)
        {
            display_->local().numeric(kDisplayIndex).setTime(time.Hours, time.Minutes);
            display_->submitLocal();
            shownMinute = time.Minutes;
        }

        // Sleep until the next minute. The tick (HSI) and the RTC (LSI) run at
        // slightly different rates, so this may wake a little early; the
        // minute is then unchanged and the next sleep covers the rest.
        osDelay((kSecondsPerMinute - time.Seconds) * 1000U);
    }
}
