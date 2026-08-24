
#include <Debug/DebugService.hpp>

#include <task.h>
#include <Debug/DebugServiceBridge.h>

#include "usbd_cdc_if.h"
#include "main.h" // HAL_GetTick

#include <cstdarg>
#include <cstdio>
#include <cstring>

DebugService& DebugService::instance()
{
    static DebugService service;
    return service;
}

DebugService::DebugService()
    : Task<1536>("DebugService", osPriorityNormal),
      logQueueHandle_(nullptr), logQueueCb_{}, logQueueStorage_{},
      rxRing_{}, rxHead_(0), rxTail_(0),
      rxLine_{}, rxLineLen_(0), rxLineTruncated_(false),
      txBuffer_{},
      sentCount_(0), droppedCount_(0), busyDropCount_(0),
      rxOverflowCount_(0), rxTruncatedCount_(0)
{
}

void DebugService::init()
{
    osMessageQueueAttr_t attr = {};
    attr.name    = "DebugLogQ";
    attr.cb_mem  = &logQueueCb_;
    attr.cb_size = sizeof(logQueueCb_);
    attr.mq_mem  = logQueueStorage_;
    attr.mq_size = sizeof(logQueueStorage_);

    logQueueHandle_ = osMessageQueueNew(kLogQueueDepth, sizeof(LogEvent), &attr);
}

const char* DebugService::levelTag(Level level)
{
    switch (level)
    {
        case Level::Info:  return "INFO";
        case Level::Warn:  return "WARN";
        case Level::Error: return "ERR";
        case Level::Debug: return "DEBUG";
        default:           return "?";
    }
}

const char* DebugService::levelColor(Level level)
{
    switch (level)
    {
        case Level::Warn:  return "\x1b[33m";
        case Level::Error: return "\x1b[31m";
        case Level::Debug: return "\x1b[90m";
        default:           return "";
    }
}

bool DebugService::log(Level level, const char* message)
{
    LogEvent event;
    event.level = level;
    char uptime[24];
    formatUptime(uptime, sizeof(uptime));
    std::snprintf(event.text, sizeof(event.text), "%s [%s] %s", uptime, levelTag(level), message);
    return enqueueLogEvent(event);
}

bool DebugService::logf(Level level, const char* format, ...)
{
    LogEvent event;
    event.level = level;
    char uptime[24];
    formatUptime(uptime, sizeof(uptime));
    int prefixLen = std::snprintf(event.text, sizeof(event.text), "%s [%s] ", uptime, levelTag(level));
    if (prefixLen < 0)
    {
        prefixLen = 0;
    }
    if (static_cast<size_t>(prefixLen) < sizeof(event.text))
    {
        va_list args;
        va_start(args, format);
        std::vsnprintf(event.text + prefixLen, sizeof(event.text) - static_cast<size_t>(prefixLen), format, args);
        va_end(args);
    }
    return enqueueLogEvent(event);
}

bool DebugService::enqueueLogEvent(const LogEvent& event)
{
    if (logQueueHandle_ == nullptr)
    {
        return false;
    }

    osStatus_t status = osMessageQueuePut(logQueueHandle_, &event, 0, 0);
    if (status == osErrorResource)
    {
        // Queue full: drop oldest, then enqueue the newest entry.
        LogEvent discarded;
        if (osMessageQueueGet(logQueueHandle_, &discarded, nullptr, 0) == osOK)
        {
            ++droppedCount_;
        }
        status = osMessageQueuePut(logQueueHandle_, &event, 0, 0);
    }

    if (status == osOK)
    {
        osThreadFlagsSet(getHandle(), kFlagLogQueued);
        return true;
    }

    ++droppedCount_;
    return false;
}

void DebugService::onUsbRxData(const uint8_t* data, uint32_t len)
{
    for (uint32_t i = 0; i < len; ++i)
    {
        if ((rxHead_ - rxTail_) >= kRxRingSize)
        {
            ++rxOverflowCount_;
            continue;
        }
        rxRing_[rxHead_ % kRxRingSize] = data[i];
        ++rxHead_;
    }

    osThreadFlagsSet(getHandle(), kFlagRxData);
}

void DebugService::drainRxRing()
{
    while (rxTail_ != rxHead_)
    {
        uint8_t byte = rxRing_[rxTail_ % kRxRingSize];
        ++rxTail_;
        handleRxByte(byte);
    }
}

void DebugService::handleRxByte(uint8_t byte)
{
    if (byte == '\r')
    {
        return;
    }

    if (byte == '\n')
    {
        rxLine_[rxLineLen_] = '\0';
        dispatchLine();
        rxLineLen_ = 0;
        rxLineTruncated_ = false;
        return;
    }

    if (rxLineLen_ < (kMaxRxLineLen - 1))
    {
        rxLine_[rxLineLen_++] = static_cast<char>(byte);
    }
    else if (!rxLineTruncated_)
    {
        rxLineTruncated_ = true;
        ++rxTruncatedCount_;
    }
}

void DebugService::formatUptime(char* out, size_t outSize) const
{
    uint32_t totalSeconds = HAL_GetTick() / 1000U;
    uint32_t seconds = totalSeconds % 60U;
    uint32_t totalMinutes = totalSeconds / 60U;
    uint32_t minutes = totalMinutes % 60U;
    uint32_t totalHours = totalMinutes / 60U;
    uint32_t hours = totalHours % 24U;
    uint32_t days = totalHours / 24U;

    std::snprintf(out, outSize, "[%lu:%02lu:%02lu:%02lu]",
                  static_cast<unsigned long>(days), static_cast<unsigned long>(hours),
                  static_cast<unsigned long>(minutes), static_cast<unsigned long>(seconds));
}

void DebugService::dispatchLine()
{
    char uptime[24];
    formatUptime(uptime, sizeof(uptime));

    int len = std::snprintf(txBuffer_, sizeof(txBuffer_), "%s: %s\n", uptime, rxLine_);
    if (len > 0)
    {
        transmit(reinterpret_cast<uint8_t*>(txBuffer_), static_cast<uint16_t>(len));
    }
}

void DebugService::drainLogQueue()
{
    LogEvent event;
    while (logQueueHandle_ != nullptr && osMessageQueueGet(logQueueHandle_, &event, nullptr, 0) == osOK)
    {
        if (transmitLogEvent(event))
        {
            ++sentCount_;
        }
    }
}

bool DebugService::transmitLogEvent(const LogEvent& event)
{
    return transmitLine(event.text, event.level);
}

bool DebugService::transmitLine(const char* text, Level level)
{
    const char* color = levelColor(level);
    if (color[0] == '\0')
    {
        size_t len = strnlen(text, sizeof(txBuffer_) - 2);
        memcpy(txBuffer_, text, len);
        txBuffer_[len] = '\n';
        txBuffer_[len + 1] = '\0';
        return transmit(reinterpret_cast<uint8_t*>(txBuffer_), static_cast<uint16_t>(len + 1));
    }

    int len = std::snprintf(txBuffer_, sizeof(txBuffer_), "%s%s\x1b[0m\n", color, text);
    if (len <= 0)
    {
        return false;
    }

    const size_t transmitLen = static_cast<size_t>(len) < sizeof(txBuffer_)
                                   ? static_cast<size_t>(len)
                                   : sizeof(txBuffer_) - 1;
    return transmit(reinterpret_cast<uint8_t*>(txBuffer_), static_cast<uint16_t>(transmitLen));
}

bool DebugService::transmit(const uint8_t* data, uint16_t len)
{
    if (len == 0)
    {
        return true;
    }

    uint32_t elapsedMs = 0;
    for (;;)
    {
        uint8_t result = CDC_Transmit_FS(const_cast<uint8_t*>(data), len);
        if (result == USBD_OK)
        {
            return true;
        }
        if (result != USBD_BUSY || elapsedMs >= kTxRetryWindowMs)
        {
            ++busyDropCount_;
            return false;
        }
        osDelay(kTxRetryDelayMs);
        elapsedMs += kTxRetryDelayMs;
    }
}

void DebugService::emitStats()
{
    char uptime[24];
    formatUptime(uptime, sizeof(uptime));

    char line[96];
    std::snprintf(line, sizeof(line),
                  "%s [STATS] sent=%lu dropped=%lu busyDrop=%lu rxOverflow=%lu rxTrunc=%lu",
                  uptime,
                  static_cast<unsigned long>(sentCount_), static_cast<unsigned long>(droppedCount_),
                  static_cast<unsigned long>(busyDropCount_), static_cast<unsigned long>(rxOverflowCount_),
                  static_cast<unsigned long>(rxTruncatedCount_));
    transmitLine(line, Level::Debug);

    std::snprintf(line, sizeof(line), "%s [MEM] heapFree=%lu heapMin=%lu",
                  uptime,
                  static_cast<unsigned long>(xPortGetFreeHeapSize()),
                  static_cast<unsigned long>(xPortGetMinimumEverFreeHeapSize()));
    transmitLine(line, Level::Debug);

    struct StackReportContext
    {
        DebugService* service;
        const char* uptime;
    } context{this, uptime};

    TaskBase::visitAll([](const TaskBase& task, void* rawContext) {
        auto& report = *static_cast<StackReportContext*>(rawContext);
        const osThreadId_t handle = task.getHandle();
        if (handle == nullptr)
        {
            return;
        }

        const auto remainingWords = uxTaskGetStackHighWaterMark(reinterpret_cast<TaskHandle_t>(handle));
        char taskLine[96];
        std::snprintf(taskLine, sizeof(taskLine),
                      "%s [STACK] name=%s configured=%lu remaining=%lu",
                      report.uptime, task.getName(),
                      static_cast<unsigned long>(task.getStackSizeBytes()),
                      static_cast<unsigned long>(remainingWords * sizeof(StackType_t)));
        report.service->transmitLine(taskLine, Level::Debug);
    }, &context);
}

void DebugService::run()
{
    for (;;)
    {
        uint32_t flags = osThreadFlagsWait(kFlagRxData | kFlagLogQueued, osFlagsWaitAny, kStatsPeriodMs);

        if (flags == osFlagsErrorTimeout)
        {
            emitStats();
            continue;
        }
        if ((flags & osFlagsError) != 0U)
        {
            continue;
        }
        if ((flags & kFlagRxData) != 0U)
        {
            drainRxRing();
        }
        drainLogQueue();
    }
}

extern "C" void DebugService_OnUsbRxData(const uint8_t* data, uint32_t len)
{
    DebugService::instance().onUsbRxData(data, len);
}
