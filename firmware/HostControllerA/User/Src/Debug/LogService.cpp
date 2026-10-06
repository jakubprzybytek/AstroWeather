 #include <Debug/LogService.hpp>

#include <Clock/ClockTask.hpp>

#include <task.h>
#include "usbd_cdc_if.h"
#include "main.h"

#include <cstdarg>
#include <cstddef>
#include <cstdio>
#include <cstring>

namespace {

// The error log's storage, in the retained RAM region (.noinit, see
// STM32G0B1xx_FLASH.ld): not cleared at startup and at the same address in
// every build, so it survives a reset and reflashing.
__attribute__((section(".noinit"))) ErrorLog::Storage errorLogStorage;

ErrorLog::Stamp stampNow()
{
    uint32_t seconds = 0U;
    if (ClockTask::instance().wallSecondsNow(seconds)) {
        return {true, seconds};
    }
    return {false, HAL_GetTick() / 1000U};
}

} // namespace

LogService& LogService::instance()
{
    static LogService service;
    return service;
}

LogService::LogService()
    : Task<1536>("LogService", osPriorityNormal),
      logQueueHandle_(nullptr), logQueueCb_{}, logQueueStorage_{}, txBuffer_{},
      sentCount_(0), droppedCount_(0), busyDropCount_(0), statsEnabled_(false),
      errorLog_(errorLogStorage)
{
}

void LogService::init()
{
    errorLog_.begin();
    errorLogStarted_ = true;
    osMessageQueueAttr_t attr = {};
    attr.name = "DebugLogQ";
    attr.cb_mem = &logQueueCb_;
    attr.cb_size = sizeof(logQueueCb_);
    attr.mq_mem = logQueueStorage_;
    attr.mq_size = sizeof(logQueueStorage_);
    logQueueHandle_ = osMessageQueueNew(kLogQueueDepth, sizeof(LogEvent), &attr);
}

const char* LogService::levelTag(Level level)
{
    switch (level) {
        case Level::Info: return "INFO";
        case Level::Warn: return "WARN";
        case Level::Error: return "ERR";
        case Level::Debug: return "DEBUG";
        default: return "?";
    }
}

const char* LogService::levelColor(Level level)
{
    switch (level) {
        case Level::Warn: return "\x1b[33m";
        case Level::Error: return "\x1b[31m";
        case Level::Debug: return "\x1b[90m";
        default: return "";
    }
}

void LogService::keepProblem(Level level, const char* message)
{
    // Before init() the storage is unchecked power-up RAM.
    if ((level != Level::Warn && level != Level::Error) || !errorLogStarted_) {
        return;
    }
    const ErrorLog::Stamp stamp = stampNow();
    const uint16_t hash = ErrorLog::textHash(message);
    // Masked rather than a scheduler critical section, so this is also safe
    // from an interrupt. It copies up to one entry, under 200 bytes.
    const UBaseType_t mask = taskENTER_CRITICAL_FROM_ISR();
    errorLog_.record(level == Level::Error ? ErrorLog::Level::Error : ErrorLog::Level::Warning,
                     message, hash, stamp);
    taskEXIT_CRITICAL_FROM_ISR(mask);
}

void LogService::errorLogSnapshot(ErrorLog::Storage& copy) const
{
    // The header and order, then one entry at a time: the whole log is over
    // 4 KB, too long to keep the USB and display refresh interrupts masked.
    // Each entry is consistent; one recorded in between shows in its new
    // state, or not at all if it took a new slot.
    UBaseType_t mask = taskENTER_CRITICAL_FROM_ISR();
    std::memcpy(&copy, &errorLogStorage, offsetof(ErrorLog::Storage, entries));
    taskEXIT_CRITICAL_FROM_ISR(mask);
    for (uint8_t i = 0U; i < ErrorLog::kCapacity; ++i) {
        mask = taskENTER_CRITICAL_FROM_ISR();
        copy.entries[i] = errorLogStorage.entries[i];
        taskEXIT_CRITICAL_FROM_ISR(mask);
    }
}

void LogService::clearErrorLog()
{
    const UBaseType_t mask = taskENTER_CRITICAL_FROM_ISR();
    errorLog_.clear();
    taskEXIT_CRITICAL_FROM_ISR(mask);
}

bool LogService::log(Level level, const char* message)
{
    keepProblem(level, message);
    LogEvent event;
    event.level = level;
    char uptime[24];
    formatUptime(uptime, sizeof(uptime));
    std::snprintf(event.text, sizeof(event.text), "%s [%s] %s", uptime, levelTag(level), message);
    return enqueueLogEvent(event);
}

bool LogService::logf(Level level, const char* format, ...)
{
    LogEvent event;
    event.level = level;
    char uptime[24];
    formatUptime(uptime, sizeof(uptime));
    int prefixLen = std::snprintf(event.text, sizeof(event.text), "%s [%s] ", uptime, levelTag(level));
    if (prefixLen < 0) prefixLen = 0;
    if (static_cast<size_t>(prefixLen) < sizeof(event.text)) {
        va_list args;
        va_start(args, format);
        std::vsnprintf(event.text + prefixLen, sizeof(event.text) - static_cast<size_t>(prefixLen), format, args);
        va_end(args);
        keepProblem(level, event.text + prefixLen);
    }
    return enqueueLogEvent(event);
}

bool LogService::sendLine(const char* message)
{
    return log(Level::Info, message);
}

bool LogService::enqueueLogEvent(const LogEvent& event)
{
    if (logQueueHandle_ == nullptr) return false;
    osStatus_t status = osMessageQueuePut(logQueueHandle_, &event, 0, 0);
    if (status == osErrorResource) {
        LogEvent discarded;
        if (osMessageQueueGet(logQueueHandle_, &discarded, nullptr, 0) == osOK) ++droppedCount_;
        status = osMessageQueuePut(logQueueHandle_, &event, 0, 0);
    }
    if (status == osOK) {
        osThreadFlagsSet(getHandle(), kFlagLogQueued);
        return true;
    }
    ++droppedCount_;
    return false;
}

void LogService::formatUptime(char* out, size_t outSize) const
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

void LogService::drainLogQueue()
{
    LogEvent event;
    while (logQueueHandle_ != nullptr && osMessageQueueGet(logQueueHandle_, &event, nullptr, 0) == osOK) {
        if (transmitLogEvent(event)) ++sentCount_;
    }
}

bool LogService::transmitLogEvent(const LogEvent& event)
{
    return transmitLine(event.text, event.level);
}

bool LogService::transmitLine(const char* text, Level level)
{
    const char* color = levelColor(level);
    if (color[0] == '\0') {
        size_t len = strnlen(text, sizeof(txBuffer_) - 2);
        memcpy(txBuffer_, text, len);
        txBuffer_[len] = '\n';
        txBuffer_[len + 1] = '\0';
        return transmit(reinterpret_cast<uint8_t*>(txBuffer_), static_cast<uint16_t>(len + 1));
    }
    int len = std::snprintf(txBuffer_, sizeof(txBuffer_), "%s%s\x1b[0m\n", color, text);
    if (len <= 0) return false;
    const size_t transmitLen = static_cast<size_t>(len) < sizeof(txBuffer_)
        ? static_cast<size_t>(len) : sizeof(txBuffer_) - 1;
    return transmit(reinterpret_cast<uint8_t*>(txBuffer_), static_cast<uint16_t>(transmitLen));
}

bool LogService::transmit(const uint8_t* data, uint16_t len)
{
    if (len == 0) return true;
    uint32_t elapsedMs = 0;
    for (;;) {
        uint8_t result = CDC_Transmit_FS(const_cast<uint8_t*>(data), len);
        if (result == USBD_OK) return true;
        if (result != USBD_BUSY || elapsedMs >= kTxRetryWindowMs) {
            ++busyDropCount_;
            return false;
        }
        osDelay(kTxRetryDelayMs);
        elapsedMs += kTxRetryDelayMs;
    }
}

void LogService::emitStats()
{
    char uptime[24];
    formatUptime(uptime, sizeof(uptime));
    char line[96];
    std::snprintf(line, sizeof(line), "%s [STATS] sent=%lu dropped=%lu busyDrop=%lu",
                  uptime, static_cast<unsigned long>(sentCount_),
                  static_cast<unsigned long>(droppedCount_), static_cast<unsigned long>(busyDropCount_));
    transmitLine(line, Level::Debug);
    std::snprintf(line, sizeof(line), "%s [MEM] heapFree=%lu heapMin=%lu", uptime,
                  static_cast<unsigned long>(xPortGetFreeHeapSize()),
                  static_cast<unsigned long>(xPortGetMinimumEverFreeHeapSize()));
    transmitLine(line, Level::Debug);
    struct StackReportContext { LogService* service; const char* uptime; } context{this, uptime};
    TaskBase::visitAll([](const TaskBase& task, void* rawContext) {
        auto& report = *static_cast<StackReportContext*>(rawContext);
        const osThreadId_t handle = task.getHandle();
        if (handle == nullptr) return;
        const auto remainingWords = uxTaskGetStackHighWaterMark(reinterpret_cast<TaskHandle_t>(handle));
        char taskLine[96];
        std::snprintf(taskLine, sizeof(taskLine), "%s [STACK] name=%s configured=%lu remaining=%lu",
                      report.uptime, task.getName(), static_cast<unsigned long>(task.getStackSizeBytes()),
                      static_cast<unsigned long>(remainingWords * sizeof(StackType_t)));
        report.service->transmitLine(taskLine, Level::Debug);
    }, &context);
}

void LogService::setStatsEnabled(bool enabled)
{
    statsEnabled_ = enabled;
    // Wake the task so it drops or adopts the stats deadline straight away
    // instead of after its current wait.
    const osThreadId_t handle = getHandle();
    if (handle != nullptr) {
        osThreadFlagsSet(handle, kFlagStatsChanged);
    }
}

void LogService::run()
{
    // Stats run on a fixed schedule rather than after a quiet period, so they
    // keep coming while the log is busy. The wait is bounded by the next
    // deadline only while stats are enabled.
    uint32_t nextStats = osKernelGetTickCount() + kStatsPeriodMs;
    for (;;) {
        uint32_t timeout = osWaitForever;
        if (statsEnabled_) {
            const int32_t remaining = static_cast<int32_t>(nextStats - osKernelGetTickCount());
            timeout = (remaining > 0) ? static_cast<uint32_t>(remaining) : 0U;
        }

        const uint32_t flags =
            osThreadFlagsWait(kFlagLogQueued | kFlagStatsChanged, osFlagsWaitAny, timeout);
        const bool woken = (flags & osFlagsError) == 0U;

        if (woken && (flags & kFlagStatsChanged) != 0U && statsEnabled_) {
            // Report once as soon as stats are switched on, then every period.
            nextStats = osKernelGetTickCount();
        }
        drainLogQueue();

        if (statsEnabled_ &&
            static_cast<int32_t>(osKernelGetTickCount() - nextStats) >= 0) {
            emitStats();
            nextStats += kStatsPeriodMs;
            // After a long stall, resynchronise rather than emitting a burst to
            // catch up on missed periods.
            if (static_cast<int32_t>(osKernelGetTickCount() - nextStats) >= 0) {
                nextStats = osKernelGetTickCount() + kStatsPeriodMs;
            }
        }
    }
}
