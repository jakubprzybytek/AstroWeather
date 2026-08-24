#include <HostController/MainLoopTask.hpp>

#include <Debug/DebugService.hpp>
#include <St67ServiceTask.hpp>

#include "app_config.h"

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

const char* statusName(HostController::St67FetchStatus status)
{
    switch (status)
    {
    case HostController::St67FetchStatus::Success:
        return "success";
    case HostController::St67FetchStatus::Busy:
        return "busy";
    case HostController::St67FetchStatus::InvalidArgument:
        return "invalid-argument";
    case HostController::St67FetchStatus::DriverFailure:
        return "driver-failure";
    case HostController::St67FetchStatus::NetworkFailure:
        return "network-failure";
    case HostController::St67FetchStatus::HttpFailure:
        return "http-failure";
    case HostController::St67FetchStatus::ResponseTooLarge:
        return "response-too-large";
    case HostController::St67FetchStatus::CleanupFailure:
        return "cleanup-failure";
    }
    return "unknown";
}

void printResponse(const uint8_t* data, uint32_t length)
{
    for (uint32_t offset = 0U; offset < length; offset += 64U)
    {
        char chunk[65];
        const uint32_t remaining = length - offset;
        const uint32_t chunkLength = remaining < 64U ? remaining : 64U;
        for (uint32_t index = 0U; index < chunkLength; ++index)
        {
            const uint8_t value = data[offset + index];
            chunk[index] = (value >= 32U && value <= 126U)
                ? static_cast<char>(value)
                : '.';
        }
        chunk[chunkLength] = '\0';
        DebugService::instance().logf(
            DebugService::Level::Info,
            "MainLoopTask response offset=%lu data=\"%s\"",
            static_cast<unsigned long>(offset), chunk);
    }
}

}  // namespace

MainLoopTask& MainLoopTask::instance()
{
    static MainLoopTask task;
    return task;
}

MainLoopTask::MainLoopTask()
    : Task<1536>("MainLoopTask", osPriorityNormal)
{
}

void MainLoopTask::trigger()
{
    MainLoopTask& task = instance();
    if (task.active_)
    {
        DebugService::instance().log(DebugService::Level::Warn,
                                     "MainLoopTask trigger ignored: active");
        return;
    }

    task.active_ = true;
    osThreadFlagsSet(task.getHandle(), kFlagRun);
}

void MainLoopTask::run()
{
    for (;;)
    {
        const uint32_t flags = osThreadFlagsWait(kFlagRun, osFlagsWaitAny,
                                                 osWaitForever);
        if ((flags & osFlagsError) != 0U)
        {
            active_ = false;
            continue;
        }

        HostController::St67FetchRequest request{};
        request.buffer = responseBuffer_;
        request.capacity = sizeof(responseBuffer_);
        const bool fetchSucceeded = HostController::FetchSt67Data(&request);

        const HostController::St67FetchResult& result = request.result;
        DebugService::instance().logf(
            fetchSucceeded ? DebugService::Level::Info : DebugService::Level::Error,
            "MainLoopTask fetch status=%s http=%u bytes=%lu crc=%08lx detail=%ld",
            statusName(result.status),
            static_cast<unsigned int>(result.httpStatus),
            static_cast<unsigned long>(result.length),
            static_cast<unsigned long>(result.crc32),
            static_cast<long>(result.detail));

        if (fetchSucceeded)
        {
            const bool responseIntegrityValid =
                calculateCrc32(responseBuffer_, result.length) == result.crc32;
            DebugService::instance().logf(
                responseIntegrityValid ? DebugService::Level::Info
                                       : DebugService::Level::Error,
                "MainLoopTask response processed bytes=%lu crc-valid=%u",
                static_cast<unsigned long>(result.length),
                responseIntegrityValid ? 1U : 0U);
            if (responseIntegrityValid)
            {
                printResponse(responseBuffer_, result.length);
            }
        }

        active_ = false;
    }
}