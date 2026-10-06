/*
 * LogService.hpp
 *
 * Centralized USB CDC debug output service. A single consumer task owns CDC_Transmit_FS.
 */

#ifndef INC_DEBUG_LOGSERVICE_HPP_
#define INC_DEBUG_LOGSERVICE_HPP_

#include <Debug/ErrorLog.hpp>
#include <Utils/Task.hpp>
#include "FreeRTOS.h"
#include "queue.h"

#include <cstddef>
#include <cstdint>

class LogService : public Task<1536>
{
public:
    enum class Level : uint8_t { Info, Warn, Error, Debug };

    static LogService& instance();
    // Also starts the error log for this boot; call once, early.
    void init();

    // Every Warn and Error logged is also kept here; see ErrorLog.hpp. The
    // entries are written under a critical section in log()/logf(); readers
    // (the console) take a consistent copy with errorLogSnapshot().
    const ErrorLog::Log& errorLog() const { return errorLog_; }
    void errorLogSnapshot(ErrorLog::Storage& copy) const;
    void clearErrorLog();
    bool log(Level level, const char* message);
    bool logf(Level level, const char* format, ...);
    bool sendLine(const char* message);

    // Periodic [STATS]/[MEM]/[STACK] telemetry, every kStatsPeriodMs on a fixed
    // schedule regardless of other log traffic. Off at boot; not persisted.
    void setStatsEnabled(bool enabled);
    bool statsEnabled() const { return statsEnabled_; }

protected:
    void run() override;

private:
    LogService();

    static constexpr uint32_t kMaxLogMessageLen = 256;
    static constexpr uint32_t kLogQueueDepth    = 16;
    static constexpr uint32_t kTxRetryWindowMs  = 40;
    static constexpr uint32_t kTxRetryDelayMs   = 5;
    static constexpr uint32_t kStatsPeriodMs    = 5000;
    static constexpr uint32_t kFlagLogQueued = 1u << 0;
    static constexpr uint32_t kFlagStatsChanged = 1u << 1;

    struct LogEvent { Level level; char text[kMaxLogMessageLen]; };

    bool enqueueLogEvent(const LogEvent& event);
    void keepProblem(Level level, const char* message);
    void drainLogQueue();
    void emitStats();
    void formatUptime(char* out, size_t outSize) const;
    bool transmit(const uint8_t* data, uint16_t len);
    bool transmitLogEvent(const LogEvent& event);
    bool transmitLine(const char* text, Level level = Level::Info);
    static const char* levelTag(Level level);
    static const char* levelColor(Level level);

    osMessageQueueId_t logQueueHandle_;
    StaticQueue_t logQueueCb_;
    uint8_t logQueueStorage_[kLogQueueDepth * sizeof(LogEvent)];
    char txBuffer_[kMaxLogMessageLen + 32];
    uint32_t sentCount_;
    uint32_t droppedCount_;
    uint32_t busyDropCount_;
    volatile bool statsEnabled_;
    ErrorLog::Log errorLog_;
    volatile bool errorLogStarted_ = false;
};

#endif /* INC_DEBUG_LOGSERVICE_HPP_ */
