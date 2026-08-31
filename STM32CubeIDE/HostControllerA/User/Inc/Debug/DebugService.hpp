/*
 * DebugService.hpp
 *
 * Centralized USB CDC debug output service. A single consumer task owns CDC_Transmit_FS.
 */

#ifndef INC_DEBUG_DEBUGSERVICE_HPP_
#define INC_DEBUG_DEBUGSERVICE_HPP_

#include <Utils/Task.hpp>
#include "FreeRTOS.h"
#include "queue.h"

#include <cstddef>
#include <cstdint>

class DebugService : public Task<1536>
{
public:
    enum class Level : uint8_t { Info, Warn, Error, Debug };

    static DebugService& instance();

    /// Creates the static log queue. Call once after osKernelInitialize(), before start().
    void init();

    /// Non-blocking log publish, safe from any task. Drops the oldest queued entry on overflow.
    bool log(Level level, const char* message);
    bool logf(Level level, const char* format, ...);

    /// Sends a response line through the same serialized CDC output as logs.
    bool sendLine(const char* message);

protected:
    void run() override;

private:
    DebugService();

    static constexpr uint32_t kMaxLogMessageLen = 200;
    static constexpr uint32_t kLogQueueDepth    = 16;
    static constexpr uint32_t kTxRetryWindowMs  = 40;
    static constexpr uint32_t kTxRetryDelayMs   = 5;
    static constexpr uint32_t kStatsPeriodMs    = 5000;

    static constexpr uint32_t kFlagLogQueued = 1u << 0;

    struct LogEvent
    {
        Level level;
        char text[kMaxLogMessageLen];
    };

    bool enqueueLogEvent(const LogEvent& event);
    void drainLogQueue();
    void emitStats();
    void formatUptime(char* out, size_t outSize) const;
    bool transmit(const uint8_t* data, uint16_t len);
    bool transmitLogEvent(const LogEvent& event);
    bool transmitLine(const char* text, Level level = Level::Info);
    static const char* levelTag(Level level);
    static const char* levelColor(Level level);

    // Message queue for log events produced by any task (drop-oldest overflow policy).
    osMessageQueueId_t logQueueHandle_;
    StaticQueue_t logQueueCb_;
    uint8_t logQueueStorage_[kLogQueueDepth * sizeof(LogEvent)];

    char txBuffer_[kMaxLogMessageLen + 32];

    uint32_t sentCount_;
    uint32_t droppedCount_;
    uint32_t busyDropCount_;
};

#endif /* INC_DEBUG_DEBUGSERVICE_HPP_ */
