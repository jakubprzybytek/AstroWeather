#include <Astro/AstroDataRefreshTask.hpp>

#include <Display/Display.hpp>
#include <Display/DisplayTypes.hpp>
#include <Astro/AstroDataParser.hpp>
#include <Astro/AstroDisplayMapper.hpp>
#include <Clock/CalendarDate.hpp>
#include <Clock/ClockTask.hpp>
#include <WiFi/St67HttpFetchTask.hpp>
#include <Utils/Crc32.hpp>

#include <Debug/LogService.hpp>

#include "FreeRTOS.h"
#include "main.h"

extern "C" RTC_HandleTypeDef hrtc;

namespace HostController {
namespace {

const char* statusName(St67FetchStatus status)
{
    switch (status)
    {
    case St67FetchStatus::Success: return "success";
    case St67FetchStatus::Busy: return "busy";
    case St67FetchStatus::InvalidArgument: return "invalid-argument";
    case St67FetchStatus::DriverFailure: return "driver-failure";
    case St67FetchStatus::NetworkFailure: return "network-failure";
    case St67FetchStatus::HttpFailure: return "http-failure";
    case St67FetchStatus::ResponseTooLarge: return "response-too-large";
    case St67FetchStatus::CleanupFailure: return "cleanup-failure";
    case St67FetchStatus::NoCredentials: return "no-wifi-credentials";
    case St67FetchStatus::Timeout: return "timeout";
    }
    return "unknown";
}

const char* parseStatusName(AstroParseStatus status)
{
    switch (status)
    {
    case AstroParseStatus::Success: return "success";
    case AstroParseStatus::InvalidArgument: return "invalid-argument";
    case AstroParseStatus::Malformed: return "malformed";
    case AstroParseStatus::UnsupportedProtocol: return "unsupported-protocol";
    case AstroParseStatus::UnsupportedBoard: return "unsupported-board";
    case AstroParseStatus::MissingRecord: return "missing-record";
    case AstroParseStatus::InvalidDisplay: return "invalid-display";
    case AstroParseStatus::InvalidTime: return "invalid-time";
    case AstroParseStatus::InvalidTemperature: return "invalid-temperature";
    case AstroParseStatus::InvalidMatrix: return "invalid-matrix";
    case AstroParseStatus::Truncated: return "truncated";
    }
    return "unknown";
}

void logRawPayload(const uint8_t* data, uint32_t length)
{
    for (uint32_t offset = 0U; offset < length; offset += 64U)
    {
        char chunk[65]{};
        const uint32_t chunkLength = (length - offset) < 64U
            ? (length - offset)
            : 64U;
        for (uint32_t index = 0U; index < chunkLength; ++index)
        {
            const uint8_t value = data[offset + index];
            chunk[index] = (value >= 32U && value <= 126U)
                ? static_cast<char>(value)
                : '.';
        }
        LogService::instance().logf(
            LogService::Level::Info,
            "AstroDataRefresh raw offset=%lu data=\"%s\"",
            static_cast<unsigned long>(offset), chunk);
    }
}

void logNumeric(uint8_t index, const AstroNumericValue& value)
{
    if (!value.available)
    {
        LogService::instance().logf(
            LogService::Level::Info, "AstroDataRefresh numeric_%u=?",
            static_cast<unsigned int>(index));
    }
    else if (value.time)
    {
        LogService::instance().logf(
            LogService::Level::Info, "AstroDataRefresh numeric_%u=%02u:%02u",
            static_cast<unsigned int>(index), static_cast<unsigned int>(value.hour),
            static_cast<unsigned int>(value.minute));
    }
    else
    {
        const int32_t tenths = static_cast<int32_t>(value.value * 10.0F);
        const int32_t magnitude = tenths < 0 ? -tenths : tenths;
        const int32_t whole = magnitude / 10;
        const int32_t fraction = magnitude % 10;
        LogService::instance().logf(
            LogService::Level::Info,
            tenths < 0 ? "AstroDataRefresh numeric_%u=-%ld.%ld"
                       : "AstroDataRefresh numeric_%u=%ld.%ld",
            static_cast<unsigned int>(index), static_cast<long>(whole),
            static_cast<long>(fraction));
    }
}

// The clock follows the server's `time` on every successful fetch; see
// docs/RTC.md. A missing or bad value leaves the clock alone but not the forecast.
void syncClock(const AstroServerTime& serverTime, uint32_t responseTick)
{
    if (!serverTime.present)
    {
        LogService::instance().log(LogService::Level::Warn,
                                   "Clock sync skipped: the response has no time record");
        return;
    }
    if (!serverTime.valid)
    {
        LogService::instance().log(LogService::Level::Warn,
                                   "Clock sync skipped: the response's time record is malformed");
        return;
    }
    ClockTask::instance().syncToServer(serverTime.value, serverTime.millisecond,
                                       serverTime.hasMilliseconds, responseTick);
}

void logWeatherFetch(const AstroWeatherFetchTime& fetch)
{
    if (!fetch.present)
    {
        LogService::instance().log(LogService::Level::Info,
                                   "AstroDataRefresh lastWeatherFetchTime absent");
    }
    else if (!fetch.valid)
    {
        LogService::instance().log(LogService::Level::Warn,
                                   "AstroDataRefresh lastWeatherFetchTime malformed");
    }
    else if (!fetch.available)
    {
        LogService::instance().log(LogService::Level::Warn,
                                   "AstroDataRefresh lastWeatherFetchTime=? (no weather)");
    }
    else
    {
        const Calendar::DateTime& t = fetch.value;
        char offset[8];
        formatUtcOffset(fetch.utcOffset, offset);
        LogService::instance().logf(
            LogService::Level::Info,
            "AstroDataRefresh lastWeatherFetchTime=%04u-%02u-%02uT%02u:%02u:%02u%s",
            static_cast<unsigned int>(t.year), static_cast<unsigned int>(t.month),
            static_cast<unsigned int>(t.day), static_cast<unsigned int>(t.hour),
            static_cast<unsigned int>(t.minute), static_cast<unsigned int>(t.second), offset);
    }
}

void logParsedData(const AstroData& data)
{
    logWeatherFetch(data.lastWeatherFetch);
    for (uint8_t displayIndex = 0U; displayIndex < data.boards.size(); ++displayIndex)
    {
        const AstroBoardData& board = data.boards[displayIndex];
        LogService::instance().logf(
            LogService::Level::Info,
            "AstroDataRefresh display=%u board=%s nightId=%s",
            static_cast<unsigned int>(displayIndex), board.board.data(),
            board.nightId.data());
        for (uint8_t numericIndex = 0U; numericIndex < board.numeric.size(); ++numericIndex)
        {
            logNumeric(numericIndex, board.numeric[numericIndex]);
        }
        for (uint8_t matrixIndex = 0U; matrixIndex < board.matrix.size(); ++matrixIndex)
        {
            LogService::instance().logf(
                LogService::Level::Info,
                "AstroDataRefresh display=%u matrix_%u=0x%08lx",
                static_cast<unsigned int>(displayIndex),
                static_cast<unsigned int>(matrixIndex),
                static_cast<unsigned long>(board.matrix[matrixIndex]));
        }
    }
}

} // namespace

const char* refreshTriggerName(RefreshTrigger trigger)
{
    switch (trigger)
    {
    case RefreshTrigger::Switch1: return "switch1";
    case RefreshTrigger::Console: return "console";
    case RefreshTrigger::Scheduled: return "scheduled";
    case RefreshTrigger::WifiTest: return "wifi-test";
    }
    return "unknown";
}

AstroDataRefreshTask& AstroDataRefreshTask::instance()
{
    static AstroDataRefreshTask task;
    return task;
}

AstroDataRefreshTask::AstroDataRefreshTask()
    : Task<3072>("AstroDataRefresh", osPriorityNormal)
{
}

void AstroDataRefreshTask::init(Display::Display* display)
{
    display_ = display;
    // The backup register goes with the time: both survive a reset and are
    // lost with power. Called after MX_RTC_Init().
    const uint32_t lastSuccess = HAL_RTCEx_BKUPRead(&hrtc, RTC_ASTRO_REFRESH_BKP_REGISTER);
    if (ClockTask::instance().isTimeSet() && lastSuccess != 0U)
    {
        scheduler_.restoreLastSuccess(lastSuccess);
    }
}

RefreshRequestResult AstroDataRefreshTask::requestRefresh(RefreshTrigger trigger)
{
    taskENTER_CRITICAL();
    if (active_)
    {
        taskEXIT_CRITICAL();
        LogService::instance().logf(
            LogService::Level::Warn,
            "AstroDataRefresh trigger ignored: active source=%s",
            refreshTriggerName(trigger));
        return RefreshRequestResult::Busy;
    }
    if (getHandle() == nullptr || display_ == nullptr)
    {
        taskEXIT_CRITICAL();
        LogService::instance().logf(
            LogService::Level::Warn,
            "AstroDataRefresh trigger unavailable source=%s",
            refreshTriggerName(trigger));
        return RefreshRequestResult::Unavailable;
    }
    active_ = true;
    trigger_ = trigger;
    const uint32_t status = osThreadFlagsSet(getHandle(), kFlagRun);
    if ((status & osFlagsError) != 0U)
    {
        active_ = false;
        taskEXIT_CRITICAL();
        LogService::instance().logf(
            LogService::Level::Error,
            "AstroDataRefresh trigger failed source=%s status=%ld",
            refreshTriggerName(trigger), static_cast<unsigned long>(status));
        return RefreshRequestResult::Unavailable;
    }
    taskEXIT_CRITICAL();
    LogService::instance().logf(
        LogService::Level::Info,
        "AstroDataRefresh trigger accepted source=%s",
        refreshTriggerName(trigger));
    return RefreshRequestResult::Accepted;
}

bool AstroDataRefreshTask::fetchPayload()
{
    request_ = {};
    request_.buffer = responseBuffer_;
    request_.capacity = sizeof(responseBuffer_);
    const bool succeeded = FetchSt67Data(&request_, &AstroDataRefreshTask::onFetchProgress, this);
    const St67FetchResult& result = request_.result;
    LogService::instance().logf(
        succeeded ? LogService::Level::Info : LogService::Level::Error,
        "AstroDataRefresh fetch status=%s http=%u bytes=%lu crc=%08lx detail=%ld",
        statusName(result.status), static_cast<unsigned int>(result.httpStatus),
        static_cast<unsigned long>(result.length),
        static_cast<unsigned long>(result.crc32), static_cast<long>(result.detail));
    if (!succeeded)
    {
        return false;
    }
    const bool integrityValid =
        Crc32::compute(responseBuffer_, result.length) == result.crc32;
    LogService::instance().logf(
        integrityValid ? LogService::Level::Info : LogService::Level::Error,
        "AstroDataRefresh response crc-valid=%u",
        integrityValid ? 1U : 0U);
    if (integrityValid)
    {
        logRawPayload(responseBuffer_, result.length);
    }
    return integrityValid;
}

bool AstroDataRefreshTask::publishDisplay(const AstroData& data)
{
    if (display_ == nullptr)
    {
        return false;
    }
    // Leaves the local bottom row to the progress bar; see AstroProgressBar.
    AstroDisplayMapper::mapAll(data, *display_);
    display_->submit();
    return true;
}

namespace {

const char* fetchStageName(FetchStage stage)
{
    switch (stage) {
    case FetchStage::Queued: return "queued";
    case FetchStage::StartingModule: return "starting-module";
    case FetchStage::JoiningWifi: return "joining-wifi";
    case FetchStage::GettingIp: return "getting-ip";
    case FetchStage::Downloading: return "downloading";
    case FetchStage::Disconnecting: return "disconnecting";
    }
    return "unknown";
}

// One verdict line for 'wifi set' / 'wifi test', so the user need not read the
// connection log to learn whether the credentials work. The specific reason for
// a failure has already been logged by the network session.
void reportWifiTest(RefreshOutcome outcome, St67FetchStatus fetchStatus, uint32_t startedTick)
{
    const WifiConnectSummary wifi = LastWifiConnect();
    const bool attemptedThisTime = static_cast<int32_t>(wifi.tick - startedTick) >= 0 &&
                                   wifi.result != WifiConnectResult::NeverTried;
    if (!attemptedThisTime) {
        LogService::instance().logf(LogService::Level::Error,
                                    "WiFi test FAILED before connecting: %s.",
                                    statusName(fetchStatus));
        return;
    }
    if (wifi.result != WifiConnectResult::Connected) {
        const bool generic = wifi.result == WifiConnectResult::Failed && wifi.reasonText[0] != '\0';
        LogService::instance().logf(LogService::Level::Error, "WiFi test FAILED for '%s': %s%s%s.",
                                    wifi.ssid, wifiConnectResultName(wifi.result),
                                    generic ? ", " : "", generic ? wifi.reasonText : "");
        return;
    }
    if (outcome == RefreshOutcome::Ok) {
        LogService::instance().logf(LogService::Level::Info,
                                    "WiFi test passed: connected to '%s' (channel %lu, %ld dBm) "
                                    "and fetched the forecast.",
                                    wifi.ssid, static_cast<unsigned long>(wifi.channel),
                                    static_cast<long>(wifi.rssi));
        return;
    }
    LogService::instance().logf(LogService::Level::Warn,
                                "WiFi test: connected to '%s' (channel %lu, %ld dBm), but the "
                                "refresh then failed: %s.",
                                wifi.ssid, static_cast<unsigned long>(wifi.channel),
                                static_cast<long>(wifi.rssi), refreshOutcomeName(outcome));
}

} // namespace

void AstroDataRefreshTask::executeRefresh(RefreshTrigger trigger)
{
    const uint32_t startedTick = osKernelGetTickCount();
    LogService::instance().logf(
        LogService::Level::Info,
        "AstroDataRefresh started source=%s", refreshTriggerName(trigger));
    loggedStage_ = 0xFFU;
    RefreshOutcome outcome = RefreshOutcome::Ok;
    if (!fetchPayload())
    {
        // FetchSt67Data succeeds exactly when the status is Success, so a
        // failure that still reads Success can only be the CRC check.
        outcome = (request_.result.status == St67FetchStatus::Success)
                      ? RefreshOutcome::IntegrityFailed
                      : RefreshOutcome::FetchFailed;
    }
    else
    {
        showProgressRow(AstroProgressBar::processingColumns());
        AstroData data{};
        const AstroParseStatus status =
            parseAstroData(responseBuffer_, request_.result.length, data);
        if (status != AstroParseStatus::Success)
        {
            outcome = RefreshOutcome::ParseFailed;
            LogService::instance().logf(
                LogService::Level::Error,
                "AstroDataRefresh parse status=%s",
                parseStatusName(status));
        }
        else
        {
            taskENTER_CRITICAL();
            last_.weatherFetchKnown = true;
            last_.lastWeatherFetch = data.lastWeatherFetch;
            taskEXIT_CRITICAL();
            logParsedData(data);
            syncClock(data.serverTime, request_.result.responseTick);
            if (!publishDisplay(data))
            {
                outcome = RefreshOutcome::PublishFailed;
                LogService::instance().log(
                    LogService::Level::Error,
                    "AstroDataRefresh display publication failed");
            }
            else
            {
                LogService::instance().log(
                    LogService::Level::Info,
                    "AstroDataRefresh display publication complete");
            }
        }
    }
    recordScheduleOutcome(outcome, request_.result.status);
    taskENTER_CRITICAL();
    last_.outcome = outcome;
    last_.trigger = trigger;
    last_.fetchStatus = request_.result.status;
    last_.httpStatus = request_.result.httpStatus;
    last_.finishedTick = osKernelGetTickCount();
    active_ = false;
    taskEXIT_CRITICAL();
    // A fetch failure is shown at the step the WiFi task stopped at; anything
    // after the download (CRC, parse, publish) at the last segment.
    if (outcome == RefreshOutcome::Ok) {
        startIndicator(AstroProgressBar::Indicator::Kind::Success, 0U);
    } else {
        startIndicator(AstroProgressBar::Indicator::Kind::Failure,
                       AstroProgressBar::failedSegment(
                           outcome == RefreshOutcome::FetchFailed, request_.stage));
    }
    LogService::instance().logf(
        LogService::Level::Info,
        "AstroDataRefresh complete source=%s outcome=%s", refreshTriggerName(trigger),
        refreshOutcomeName(outcome));
    if (trigger == RefreshTrigger::WifiTest) {
        reportWifiTest(outcome, request_.result.status, startedTick);
    }
}

RefreshSchedule::Clock AstroDataRefreshTask::readClock()
{
    RefreshSchedule::Clock clock{false, 0U, osKernelGetTickCount()};
    ClockTask::DateTime now{};
    if (ClockTask::instance().isTimeSet() && ClockTask::instance().readDateTime(now))
    {
        clock.timeSet = true;
        clock.now = Calendar::secondsSince2000(now.year, now.month, now.day, now.hour,
                                               now.minute, now.second);
    }
    return clock;
}

void AstroDataRefreshTask::checkSchedule()
{
    if (active_)
    {
        return;
    }
    if (scheduler_.due(readClock()))
    {
        (void)requestRefresh(RefreshTrigger::Scheduled);
    }
}

void AstroDataRefreshTask::recordScheduleOutcome(RefreshOutcome outcome,
                                                 St67FetchStatus fetchStatus)
{
    // Read after the refresh: a success may have just set or stepped the clock.
    const RefreshSchedule::Clock clock = readClock();
    const bool success = outcome == RefreshOutcome::Ok;
    taskENTER_CRITICAL();
    scheduler_.recordOutcome(clock, success, fetchStatus != St67FetchStatus::NoCredentials);
    taskEXIT_CRITICAL();
    if (success && clock.timeSet)
    {
        HAL_RTCEx_BKUPWrite(&hrtc, RTC_ASTRO_REFRESH_BKP_REGISTER, clock.now);
    }
    if (!clock.timeSet)
    {
        return;
    }
    const Calendar::DateTime next =
        Calendar::fromSecondsSince2000(RefreshSchedule::nextSlotStart(clock.now));
    if (success)
    {
        LogService::instance().logf(LogService::Level::Info,
                                    "AstroDataRefresh next scheduled at %02u:%02u",
                                    static_cast<unsigned int>(next.hour),
                                    static_cast<unsigned int>(next.minute));
    }
    else if (fetchStatus == St67FetchStatus::NoCredentials)
    {
        LogService::instance().logf(LogService::Level::Warn,
                                    "AstroDataRefresh no retry without WiFi credentials; "
                                    "next scheduled at %02u:%02u",
                                    static_cast<unsigned int>(next.hour),
                                    static_cast<unsigned int>(next.minute));
    }
    else
    {
        LogService::instance().logf(
            LogService::Level::Warn, "AstroDataRefresh retry %lu in %lu min",
            static_cast<unsigned long>(scheduler_.failures()),
            static_cast<unsigned long>(RefreshSchedule::retryDelayMs(scheduler_.failures()) / 60000U));
    }
}

ScheduleSummary AstroDataRefreshTask::schedule() const
{
    const RefreshSchedule::Clock clock = readClock();
    taskENTER_CRITICAL();
    const RefreshSchedule::Scheduler scheduler = scheduler_;
    taskEXIT_CRITICAL();
    ScheduleSummary summary{};
    summary.timeSet = clock.timeSet;
    summary.nextSlot = clock.timeSet ? RefreshSchedule::nextSlotStart(clock.now) : 0U;
    summary.hasSuccess = scheduler.hasSuccess();
    summary.lastSuccess = scheduler.lastSuccess();
    summary.failures = scheduler.failures();
    summary.waitingForNextSlot = scheduler.waitingForNextSlot();
    const int32_t retryIn = static_cast<int32_t>(scheduler.retryAtTick() - clock.tick);
    summary.retryInMs = (summary.failures != 0U && retryIn > 0) ? static_cast<uint32_t>(retryIn) : 0U;
    return summary;
}

RefreshSummary AstroDataRefreshTask::lastRefresh() const
{
    // Written by the refresh task; copied whole so a reader never sees half of
    // one refresh and half of the next.
    taskENTER_CRITICAL();
    RefreshSummary summary = last_;
    summary.running = active_;
    taskEXIT_CRITICAL();
    return summary;
}

const char* refreshOutcomeName(RefreshOutcome outcome)
{
    switch (outcome)
    {
    case RefreshOutcome::Never: return "never";
    case RefreshOutcome::Ok: return "ok";
    case RefreshOutcome::FetchFailed: return "fetch-failed";
    case RefreshOutcome::IntegrityFailed: return "crc-failed";
    case RefreshOutcome::ParseFailed: return "parse-failed";
    case RefreshOutcome::PublishFailed: return "publish-failed";
    }
    return "unknown";
}

const char* fetchStatusName(St67FetchStatus status)
{
    return statusName(status);
}

void AstroDataRefreshTask::onFetchProgress(FetchStage stage, void* context)
{
    AstroDataRefreshTask& self = *static_cast<AstroDataRefreshTask*>(context);
    self.showProgressRow(AstroProgressBar::fetchingColumns(stage, osKernelGetTickCount()));
    if (static_cast<uint8_t>(stage) != self.loggedStage_)
    {
        self.loggedStage_ = static_cast<uint8_t>(stage);
        LogService::instance().logf(LogService::Level::Debug, "AstroDataRefresh stage=%s",
                                    fetchStageName(stage));
    }
}

void AstroDataRefreshTask::showProgressRow(uint32_t columns)
{
    if (display_ == nullptr || columns == shownRow_)
    {
        return;
    }
    display_->local().matrix(AstroDisplayMapper::kProgressRow).setRow(columns);
    display_->submitLocal();
    shownRow_ = columns;
}

void AstroDataRefreshTask::startIndicator(AstroProgressBar::Indicator::Kind kind,
                                          uint8_t failedSegment)
{
    // Success: the full bar. Failure: the bar up to and including the step that
    // failed, blinked by stepIndicator().
    showProgressRow(indicator_.start(kind, failedSegment, osKernelGetTickCount()));
}

void AstroDataRefreshTask::stepIndicator()
{
    uint32_t columns = 0U;
    if (indicator_.step(columns))
    {
        showProgressRow(columns);
    }
}

void AstroDataRefreshTask::clearIndicator()
{
    indicator_.cancel();
    showProgressRow(0U);
}

void AstroDataRefreshTask::run()
{
    for (;;)
    {
        // Between refreshes the task wakes to check the schedule, and while an
        // outcome is shown on the progress row, to blink it and to clear it when
        // it expires. A new refresh request always takes over straight away.
        uint32_t timeout = kScheduleCheckMs;
        if (indicator_.active())
        {
            const uint32_t now = osKernelGetTickCount();
            if (indicator_.expired(now))
            {
                clearIndicator();
                continue;
            }
            timeout = indicator_.waitMs(now, timeout);
        }

        const uint32_t flags = osThreadFlagsWait(kFlagRun, osFlagsWaitAny, timeout);
        if (flags == static_cast<uint32_t>(osFlagsErrorTimeout))
        {
            stepIndicator();
            checkSchedule();
            continue;
        }
        if ((flags & osFlagsError) != 0U)
        {
            taskENTER_CRITICAL();
            active_ = false;
            taskEXIT_CRITICAL();
            continue;
        }
        indicator_.cancel();
        executeRefresh(trigger_);
    }
}

} // namespace HostController
