#include <Clock/ClockTask.hpp>

#include <Clock/CalendarDate.hpp>

#include <Debug/LogService.hpp>
#include <Display/Display.hpp>

#include "main.h"

#include <cstdio>
#include <cstring>

namespace {

constexpr uint32_t kRetryDelayMs = 1000U;
constexpr uint32_t kSecondsPerMinute = 60U;
// Never a real minute, so the next pass always draws.
constexpr uint8_t kNothingShown = 0xFFU;
constexpr uint32_t kMsPerSecond = 1000U;
constexpr int64_t kMsPerHour = 3600000;
// Beyond this the offset is not measured as drift: the RTC is simply set.
constexpr int64_t kMaxMeasuredOffsetMs = 86400000;

int64_t toMs(const Calendar::DateTime& t)
{
    return static_cast<int64_t>(
               Calendar::secondsSince2000(t.year, t.month, t.day, t.hour, t.minute, t.second)) *
           kMsPerSecond;
}

// "+1.234 s"
void formatSeconds(char* out, size_t size, int64_t ms)
{
    const uint64_t magnitude = static_cast<uint64_t>(ms < 0 ? -ms : ms);
    std::snprintf(out, size, "%c%lu.%03lu s", ms < 0 ? '-' : '+',
                  static_cast<unsigned long>(magnitude / kMsPerSecond),
                  static_cast<unsigned long>(magnitude % kMsPerSecond));
}

// "2026-09-23 09:30:12"
void formatDateTime(char* out, size_t size, const Calendar::DateTime& t)
{
    std::snprintf(out, size, "%04u-%02u-%02u %02u:%02u:%02u", static_cast<unsigned>(t.year),
                  static_cast<unsigned>(t.month), static_cast<unsigned>(t.day),
                  static_cast<unsigned>(t.hour), static_cast<unsigned>(t.minute),
                  static_cast<unsigned>(t.second));
}

// "+2.5", "-0.3": value / divisor to one decimal. The sign is kept separately
// so that values between -1 and 0 keep theirs.
void formatTenths(char* out, size_t size, int64_t value, int64_t divisor, bool withPlus)
{
    const int64_t magnitude = value < 0 ? -value : value;
    const int64_t tenths = (magnitude * 10 + divisor / 2) / divisor;
    std::snprintf(out, size, "%s%lu.%lu", value < 0 ? "-" : (withPlus ? "+" : ""),
                  static_cast<unsigned long>(tenths / 10),
                  static_cast<unsigned long>(tenths % 10));
}
// "45 s", "37 min", "6.7 h"
void formatSpan(char* out, size_t size, int64_t ms)
{
    if (ms < 120 * static_cast<int64_t>(kMsPerSecond))
    {
        std::snprintf(out, size, "%lu s",
                      static_cast<unsigned long>((ms + kMsPerSecond / 2) / kMsPerSecond));
    }
    else if (ms < 2 * kMsPerHour)
    {
        std::snprintf(out, size, "%lu min", static_cast<unsigned long>((ms + 30000) / 60000));
    }
    else
    {
        formatTenths(out, size, ms, kMsPerHour, false);
        std::strncat(out, " h", size - std::strlen(out) - 1U);
    }
}
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
    // The drift since the last sync is lost with the old time.
    driftResetPending_ = true;
    return writeDateTime({year, month, day, hour, minute, second});
}

bool ClockTask::writeDateTime(const Calendar::DateTime& dateTime)
{
    const uint16_t year = dateTime.year;
    const uint8_t month = dateTime.month;
    const uint8_t day = dateTime.day;
    const uint8_t hour = dateTime.hour;
    const uint8_t minute = dateTime.minute;
    const uint8_t second = dateTime.second;

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
    if (ppm != trimPpm_)
    {
        // A new rate: drift measured on the old one no longer applies.
        driftResetPending_ = true;
    }
    trimPpm_ = ppm;
    trimSettings_ = settings;
    return true;
}

bool ClockTask::stepToServer(int64_t serverMs, uint32_t responseTick, Calendar::DateTime& set,
                             uint32_t& lateMs)
{
    // The RTC can only be set to a whole second, and starts that second when
    // written. So wait for the server's next second to begin, then write it.
    const int64_t nowMs = serverMs + static_cast<int64_t>(osKernelGetTickCount() - responseTick);
    const uint32_t waitMs =
        (kMsPerSecond - static_cast<uint32_t>(nowMs % kMsPerSecond)) % kMsPerSecond;
    if (waitMs != 0U)
    {
        osDelay(waitMs);
    }
    const int64_t thenMs = serverMs + static_cast<int64_t>(osKernelGetTickCount() - responseTick);
    lateMs = static_cast<uint32_t>(thenMs % kMsPerSecond);
    set = Calendar::fromSecondsSince2000(static_cast<uint32_t>(thenMs / kMsPerSecond));
    return writeDateTime(set);
}

void ClockTask::syncToServer(const Calendar::DateTime& server, uint16_t millisecond,
                             bool hasMilliseconds, uint32_t responseTick)
{
    LogService& log = LogService::instance();
    if (driftResetPending_)
    {
        driftResetPending_ = false;
        drift_.reset();
    }

    const ClockSync::Precision& precision =
        hasMilliseconds ? ClockSync::kMilliseconds : ClockSync::kWholeSeconds;
    // The server's time when the headers arrived: the stamp plus the network
    // delay, and for a truncated stamp the half second lost on average.
    const int64_t serverMs = toMs(server) + millisecond + precision.serverOffsetMs;
    char serverText[32];
    formatDateTime(serverText, sizeof(serverText), server);
    if (hasMilliseconds)
    {
        const size_t used = std::strlen(serverText);
        std::snprintf(serverText + used, sizeof(serverText) - used, ".%03u",
                      static_cast<unsigned>(millisecond));
    }

    Calendar::DateTime set{};
    uint32_t lateMs = 0U;
    char setText[24];
    if (!timeSet_)
    {
        if (!stepToServer(serverMs, responseTick, set, lateMs))
        {
            log.log(LogService::Level::Error, "Clock sync: RTC write failed");
            return;
        }
        formatDateTime(setText, sizeof(setText), set);
        log.logf(LogService::Level::Info,
                 "Clock sync: server %s; the RTC was not set since power-up, now set to %s "
                 "(+%lu ms)",
                 serverText, setText, static_cast<unsigned long>(lateMs));
        drift_.reset();
        logDrift(drift_.record(serverMs, 0, true, precision.sampleErrorMs), precision);
        return;
    }

    DateTime rtc{};
    if (!readDateTime(rtc))
    {
        log.log(LogService::Level::Error, "Clock sync: RTC read failed");
        return;
    }
    const uint32_t ageMs = osKernelGetTickCount() - responseTick;
    const int64_t nowMs = serverMs + ageMs;
    const int64_t rtcMs =
        toMs({rtc.year, rtc.month, rtc.day, rtc.hour, rtc.minute, rtc.second}) + rtc.millisecond;
    const int64_t offsetMs = rtcMs - nowMs;
    char offsetText[24];
    formatSeconds(offsetText, sizeof(offsetText), offsetMs);
    log.logf(LogService::Level::Info,
             "Clock sync: server %s (response %lu ms ago), RTC %04u-%02u-%02u "
             "%02u:%02u:%02u.%03u, offset %s (RTC minus server, +-%lu ms)",
             serverText, static_cast<unsigned long>(ageMs), static_cast<unsigned>(rtc.year),
             static_cast<unsigned>(rtc.month), static_cast<unsigned>(rtc.day),
             static_cast<unsigned>(rtc.hour), static_cast<unsigned>(rtc.minute),
             static_cast<unsigned>(rtc.second), static_cast<unsigned>(rtc.millisecond),
             offsetText, static_cast<unsigned long>(precision.sampleErrorMs));

    const bool measurable = offsetMs > -kMaxMeasuredOffsetMs && offsetMs < kMaxMeasuredOffsetMs;
    const bool adjust = !measurable || offsetMs >= precision.adjustThresholdMs ||
                        offsetMs <= -precision.adjustThresholdMs;
    ClockSync::Result result{};
    if (measurable)
    {
        result = drift_.record(nowMs, static_cast<int32_t>(offsetMs), adjust,
                               precision.sampleErrorMs);
    }
    else
    {
        drift_.reset();
        result.kind = ClockSync::Kind::Step;
    }

    if (!adjust)
    {
        log.logf(LogService::Level::Info,
                 "Clock sync: RTC kept, the offset is under the %lu ms threshold",
                 static_cast<unsigned long>(precision.adjustThresholdMs));
    }
    else if (!stepToServer(serverMs, responseTick, set, lateMs))
    {
        drift_.reset();
        log.log(LogService::Level::Error, "Clock sync: RTC write failed");
        return;
    }
    else
    {
        char stepText[24];
        formatSeconds(stepText, sizeof(stepText), -offsetMs);
        formatDateTime(setText, sizeof(setText), set);
        log.logf(LogService::Level::Info,
                 "Clock sync: RTC stepped by %s, as the offset reached the %lu ms threshold; "
                 "set to %s (+%lu ms)",
                 stepText, static_cast<unsigned long>(precision.adjustThresholdMs), setText,
                 static_cast<unsigned long>(lateMs));
    }
    logDrift(result, precision);
}

void ClockTask::logDrift(const ClockSync::Result& result, const ClockSync::Precision& precision)
{
    LogService& log = LogService::instance();
    if (result.kind == ClockSync::Kind::First)
    {
        log.logf(LogService::Level::Info,
                 "Clock drift: measurement starts at this sync, trim %+ld ppm",
                 static_cast<long>(trimPpm_));
        return;
    }
    if (result.kind == ClockSync::Kind::Step)
    {
        log.log(LogService::Level::Warn,
                "Clock drift: the offset jumped further than the LSI can drift (a DST change, "
                "or the time set elsewhere); measurement restarts at this sync");
        return;
    }

    char driftText[24];
    char spanText[16];
    const ClockSync::Span& last = result.sinceLast;
    formatSeconds(driftText, sizeof(driftText), last.driftMs);
    formatSpan(spanText, sizeof(spanText), last.spanMs);
    log.logf(LogService::Level::Info,
             "Clock drift since the previous sync: %s over %s, %+ld ppm +-%ld", driftText,
             spanText, static_cast<long>(last.ppm), static_cast<long>(last.uncertaintyPpm));

    const ClockSync::Span& total = result.sinceReference;
    formatSeconds(driftText, sizeof(driftText), total.driftMs);
    formatSpan(spanText, sizeof(spanText), total.spanMs);
    // ppm * 86 400 s / 10^6 = s a day.
    char perDayText[16];
    formatTenths(perDayText, sizeof(perDayText), static_cast<int64_t>(total.ppm) * 864, 10000,
                 true);
    const uint32_t lsi = ClockSync::lsiMilliHz(trimPpm_, total.ppm);
    // 32 mHz per ppm; to 0.1 Hz.
    char lsiErrorText[16];
    formatTenths(lsiErrorText, sizeof(lsiErrorText),
                 static_cast<int64_t>(total.uncertaintyPpm) * 32, 1000, false);
    char referenceText[24];
    formatDateTime(referenceText, sizeof(referenceText),
                   Calendar::fromSecondsSince2000(
                       static_cast<uint32_t>(result.referenceMs / kMsPerSecond)));
    log.logf(LogService::Level::Info,
             "Clock drift since %s: %s over %s, %+ld ppm +-%ld, %s s/day; "
             "LSI %lu.%01lu +-%s Hz at trim %+ld ppm",
             referenceText, driftText, spanText, static_cast<long>(total.ppm),
             static_cast<long>(total.uncertaintyPpm), perDayText,
             static_cast<unsigned long>((lsi + 50U) / 1000U),
             static_cast<unsigned long>(((lsi + 50U) % 1000U) / 100U), lsiErrorText,
             static_cast<long>(trimPpm_));

    // What to do about the trim. Never applied automatically: the LSI moves
    // with the conditions, so the user picks the span it is taken from.
    if (total.uncertaintyPpm > ClockSync::kUsefulUncertaintyPpm)
    {
        const int64_t neededMs = static_cast<int64_t>(2 * precision.sampleErrorMs) *
                                 ClockSync::kPpmScale / ClockSync::kUsefulUncertaintyPpm;
        formatSpan(spanText, sizeof(spanText), neededMs);
        log.logf(LogService::Level::Info,
                 "Clock trim: too early to judge (+-%ld ppm); +-%ld ppm needs %s of syncs "
                 "with no reset, time set or time trim",
                 static_cast<long>(total.uncertaintyPpm),
                 static_cast<long>(ClockSync::kUsefulUncertaintyPpm), spanText);
    }
    else if (total.ppm <= total.uncertaintyPpm && total.ppm >= -total.uncertaintyPpm)
    {
        log.logf(LogService::Level::Info,
                 "Clock trim: %+ld ppm is right to within the +-%ld ppm error; keep it",
                 static_cast<long>(trimPpm_), static_cast<long>(total.uncertaintyPpm));
    }
    else
    {
        log.logf(LogService::Level::Info,
                 "Clock trim: 'time trim %ld' would cancel this drift (+-%ld ppm). Not applied: "
                 "the LSI moves with the conditions, so check the span is representative",
                 static_cast<long>(ClockSync::combinedTrimPpm(trimPpm_, total.ppm)),
                 static_cast<long>(total.uncertaintyPpm));
    }
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
