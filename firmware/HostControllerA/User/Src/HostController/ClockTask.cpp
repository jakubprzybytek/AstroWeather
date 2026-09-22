#include <HostController/ClockTask.hpp>

#include <HostController/CalendarDate.hpp>

#include <Debug/LogService.hpp>
#include <Display/Display.hpp>

#include "main.h"

namespace {

constexpr uint32_t kRetryDelayMs = 1000U;
constexpr uint32_t kSecondsPerMinute = 60U;
// Never a real minute, so the next pass always draws.
constexpr uint8_t kNothingShown = 0xFFU;
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
    // First used from AppVariant_Init(), after MX_RTC_Init(). The marker
    // survives a reset but not a power loss.
    timeSet_ = HAL_RTCEx_BKUPRead(&hrtc, RTC_TIME_SET_BKP_REGISTER) == RTC_TIME_SET_MARKER;
}

void ClockTask::setDisplay(Display::Display* display)
{
    display_ = display;
}

void ClockTask::setDisplayEnabled(bool enabled)
{
    displayEnabled_ = enabled;
    wake();
}

bool ClockTask::setDateTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour,
                            uint8_t minute, uint8_t second)
{
    // Leap seconds are not represented: the RTC counts 0..59.
    if (!Calendar::isValidDate(year, month, day) || hour > 23U || minute > 59U || second > 59U)
    {
        return false;
    }

    // The RTC holds a two-digit year and does not work out the weekday itself.
    // A non-zero year also marks the calendar as initialised (RTC_ICSR.INITS),
    // so that HAL_RTC_Init() leaves it and the prescalers alone after a reset.
    // Set before the time: each write restarts the current second.
    RTC_DateTypeDef date = {};
    date.WeekDay = Calendar::dayOfWeek(year, month, day);
    date.Month = month;
    date.Date = day;
    date.Year = static_cast<uint8_t>(year - Calendar::kMinYear);
    RTC_TimeTypeDef time = {};
    time.Hours = hour;
    time.Minutes = minute;
    time.Seconds = second;
    time.DayLightSaving = RTC_DAYLIGHTSAVING_NONE;
    time.StoreOperation = RTC_STOREOPERATION_RESET;
    bool ok;
    {
        MutexGuard guard(rtcMutex_);
        ok = HAL_RTC_SetDate(&hrtc, &date, RTC_FORMAT_BIN) == HAL_OK &&
             HAL_RTC_SetTime(&hrtc, &time, RTC_FORMAT_BIN) == HAL_OK;
        if (ok)
        {
            HAL_RTCEx_BKUPWrite(&hrtc, RTC_TIME_SET_BKP_REGISTER, RTC_TIME_SET_MARKER);
        }
    }
    if (ok)
    {
        timeSet_ = true;
        wake();
    }
    return ok;
}

bool ClockTask::readDateTime(DateTime& dateTime)
{
    RTC_TimeTypeDef rtcTime = {};
    RTC_DateTypeDef rtcDate = {};
    bool ok;
    {
        MutexGuard guard(rtcMutex_);
        // GetDate must follow GetTime: it unlocks the shadow registers that
        // GetTime froze, otherwise the time stops advancing.
        ok = HAL_RTC_GetTime(&hrtc, &rtcTime, RTC_FORMAT_BIN) == HAL_OK &&
             HAL_RTC_GetDate(&hrtc, &rtcDate, RTC_FORMAT_BIN) == HAL_OK;
    }
    if (!ok)
    {
        return false;
    }
    dateTime.year = static_cast<uint16_t>(Calendar::kMinYear + rtcDate.Year);
    dateTime.month = rtcDate.Month;
    dateTime.day = rtcDate.Date;
    dateTime.hour = rtcTime.Hours;
    dateTime.minute = rtcTime.Minutes;
    dateTime.second = rtcTime.Seconds;
    // The sub-second counter counts down from PREDIV_S (SecondFraction).
    const uint32_t divider = rtcTime.SecondFraction + 1U;
    dateTime.millisecond = static_cast<uint16_t>(
        ((rtcTime.SecondFraction - rtcTime.SubSeconds) * 1000U) / divider);
    return true;
}

bool ClockTask::setTrim(int32_t ppm)
{
    RtcTrim::Settings settings{};
    if (!RtcTrim::compute(ppm, settings))
    {
        return false;
    }

    bool ok = true;
    {
        MutexGuard guard(rtcMutex_);
        // The prescalers can only be written in initialisation mode, which
        // keeps the time but loses the part of the second already counted, up
        // to a second. So they are only rewritten when they change; the
        // calibration alone needs no initialisation mode. They are compared
        // with the register, which after a reset still holds the trim from
        // before it.
        const uint32_t prer = hrtc.Instance->PRER;
        if (settings.asynchPrediv != ((prer & RTC_PRER_PREDIV_A) >> RTC_PRER_PREDIV_A_Pos) ||
            settings.synchPrediv != (prer & RTC_PRER_PREDIV_S))
        {
            __HAL_RTC_WRITEPROTECTION_DISABLE(&hrtc);
            ok = RTC_EnterInitMode(&hrtc) == HAL_OK;
            if (ok)
            {
                hrtc.Instance->PRER = settings.synchPrediv;
                hrtc.Instance->PRER |= settings.asynchPrediv << RTC_PRER_PREDIV_A_Pos;
                ok = RTC_ExitInitMode(&hrtc) == HAL_OK;
            }
            __HAL_RTC_WRITEPROTECTION_ENABLE(&hrtc);
        }
        if (ok)
        {
            hrtc.Init.AsynchPrediv = settings.asynchPrediv;
            hrtc.Init.SynchPrediv = settings.synchPrediv;
            ok = HAL_RTCEx_SetSmoothCalib(&hrtc, RTC_SMOOTHCALIB_PERIOD_32SEC,
                                          RTC_SMOOTHCALIB_PLUSPULSES_RESET,
                                          settings.minusPulses) == HAL_OK;
        }
    }
    if (!ok)
    {
        LogService::instance().logf(LogService::Level::Error,
                                    "Clock trim %ld ppm rejected by the RTC",
                                    static_cast<long>(ppm));
        return false;
    }
    trimPpm_ = ppm;
    trimSettings_ = settings;
    return true;
}

void ClockTask::wake()
{
    // Before start() there is no thread, and run() draws on its first pass.
    if (getHandle() != nullptr)
    {
        osThreadFlagsSet(getHandle(), kFlagRedraw);
    }
}

void ClockTask::run()
{
    uint8_t shownMinute = kNothingShown;

    for (;;)
    {
        if (!displayEnabled_)
        {
            // Blank, then wait to be switched back on. This runs once per wake,
            // so also after a 'time set' while off, which blanks it again.
            if (display_ != nullptr)
            {
                display_->local().numeric(kDisplayIndex).setBlank();
                display_->submitLocal();
            }
            shownMinute = kNothingShown;
            (void)osThreadFlagsWait(kFlagRedraw, osFlagsWaitAny, osWaitForever);
            continue;
        }

        if (!timeSet_)
        {
            // Nothing to count until the time is set, which wakes the task.
            if (display_ != nullptr)
            {
                display_->local().numeric(kDisplayIndex).setTimeUnset();
                display_->submitLocal();
            }
            shownMinute = kNothingShown;
            (void)osThreadFlagsWait(kFlagRedraw, osFlagsWaitAny, osWaitForever);
            continue;
        }

        DateTime time{};
        if (!readDateTime(time))
        {
            LogService::instance().log(LogService::Level::Error,
                                       "Clock RTC read failed");
            osDelay(kRetryDelayMs);
            continue;
        }

        if (time.minute != shownMinute && display_ != nullptr)
        {
            display_->local().numeric(kDisplayIndex).setTime(time.hour, time.minute);
            display_->submitLocal();
            shownMinute = time.minute;
        }

        // Sleep until the next minute, or until the time is set or the display
        // switched. The tick (HSI) and the RTC (LSI) run at slightly different
        // rates, so this may wake a little early; the minute is then unchanged
        // and the next sleep covers the rest.
        const uint32_t flags = osThreadFlagsWait(
            kFlagRedraw, osFlagsWaitAny, (kSecondsPerMinute - time.second) * 1000U);
        if ((flags & osFlagsError) == 0U)
        {
            // A new time can keep the same minute, so force the redraw.
            shownMinute = kNothingShown;
        }
    }
}
