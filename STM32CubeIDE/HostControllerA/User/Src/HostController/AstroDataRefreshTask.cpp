#include <HostController/AstroDataRefreshTask.hpp>

#include <Display/Display.hpp>
#include <Display/DisplayTypes.hpp>
#include <HostController/AstroDataParser.hpp>
#include <St67HttpFetchTask.hpp>

#include <Debug/LogService.hpp>

#include "FreeRTOS.h"

namespace HostController {
namespace {

uint32_t calculateCrc32(const uint8_t* data, uint32_t length)
{
    uint32_t crc = 0xFFFFFFFFU;
    for (uint32_t index = 0U; index < length; ++index)
    {
        crc ^= data[index];
        for (uint32_t bit = 0U; bit < 8U; ++bit)
        {
            crc = (crc & 1U) != 0U
                ? (crc >> 1U) ^ 0xEDB88320U
                : (crc >> 1U);
        }
    }
    return crc ^ 0xFFFFFFFFU;
}

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

void setNumeric(Display::NumericDisplay display,
                const AstroNumericValue& value)
{
    if (!value.available)
    {
        Display::NumericSegments unavailable{};
        unavailable.slots.fill(Display::kSegmentDp);
        unavailable.slots[4] = 0U;
        display.setSegments(unavailable);
    }
    else if (value.time)
    {
        display.setTime(value.hour, value.minute);
    }
    else
    {
        display.setValue(value.value, 1U);
    }
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

void logParsedData(const AstroData& data)
{
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
    }
    return "unknown";
}

AstroDataRefreshTask& AstroDataRefreshTask::instance()
{
    static AstroDataRefreshTask task;
    return task;
}

AstroDataRefreshTask::AstroDataRefreshTask()
    : Task<2048>("AstroDataRefresh", osPriorityNormal)
{
}

void AstroDataRefreshTask::init(Display::Display* display)
{
    display_ = display;
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
    const bool succeeded = FetchSt67Data(&request_);
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
        calculateCrc32(responseBuffer_, result.length) == result.crc32;
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
    for (uint8_t boardIndex = 0U; boardIndex < data.boards.size(); ++boardIndex)
    {
        Display::DisplayBoard& board = boardIndex == 0U
            ? display_->local()
            : display_->remote(static_cast<uint8_t>(boardIndex - 1U));
        for (uint8_t numericIndex = 0U;
             numericIndex < data.boards[boardIndex].numeric.size(); ++numericIndex)
        {
            setNumeric(board.numeric(numericIndex),
                       data.boards[boardIndex].numeric[numericIndex]);
        }
        for (uint8_t matrixIndex = 0U; matrixIndex < 4U; ++matrixIndex)
        {
            board.matrix(matrixIndex).setRow(data.boards[boardIndex].matrix[matrixIndex]);
        }
        board.matrix(4U).setRow(0U);
    }
    display_->submit();
    return true;
}

void AstroDataRefreshTask::executeRefresh(RefreshTrigger trigger)
{
    LogService::instance().logf(
        LogService::Level::Info,
        "AstroDataRefresh started source=%s", refreshTriggerName(trigger));
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
            logParsedData(data);
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
    taskENTER_CRITICAL();
    last_.outcome = outcome;
    last_.trigger = trigger;
    last_.fetchStatus = request_.result.status;
    last_.httpStatus = request_.result.httpStatus;
    last_.finishedTick = osKernelGetTickCount();
    active_ = false;
    taskEXIT_CRITICAL();
    LogService::instance().logf(
        LogService::Level::Info,
        "AstroDataRefresh complete source=%s outcome=%s", refreshTriggerName(trigger),
        refreshOutcomeName(outcome));
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

void AstroDataRefreshTask::run()
{
    for (;;)
    {
        const uint32_t flags = osThreadFlagsWait(kFlagRun, osFlagsWaitAny,
                                                 osWaitForever);
        if ((flags & osFlagsError) != 0U)
        {
            taskENTER_CRITICAL();
            active_ = false;
            taskEXIT_CRITICAL();
            continue;
        }
        executeRefresh(trigger_);
    }
}

} // namespace HostController
