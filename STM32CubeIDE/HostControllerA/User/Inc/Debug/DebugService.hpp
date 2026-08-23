/*
 * DebugService.hpp
 *
 * Centralized USB CDC debug service: task-safe logging from multiple producers
 * plus line-based host healthcheck echo. A single consumer task owns CDC_Transmit_FS.
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
    enum class Level : uint8_t { Info, Warn, Error };

    static DebugService& instance();

    /// Creates the static log queue. Call once after osKernelInitialize(), before start().
    void init();

    /// Non-blocking log publish, safe from any task. Drops the oldest queued entry on overflow.
    bool log(Level level, const char* message);
    bool logf(Level level, const char* format, ...);

    /// USB CDC RX ingress bridge; safe to call from USB interrupt context.
    void onUsbRxData(const uint8_t* data, uint32_t len);

protected:
    void run() override;

private:
    DebugService();

    static constexpr uint32_t kMaxLogMessageLen = 200;
    static constexpr uint32_t kMaxRxLineLen     = 96;
    static constexpr uint32_t kRxRingSize       = 256;
    static constexpr uint32_t kLogQueueDepth    = 64;
    static constexpr uint32_t kTxRetryWindowMs  = 40;
    static constexpr uint32_t kTxRetryDelayMs   = 5;
    static constexpr uint32_t kStatsPeriodMs    = 5000;

    static constexpr uint32_t kFlagRxData    = 1u << 0;
    static constexpr uint32_t kFlagLogQueued = 1u << 1;

    struct LogEvent
    {
        char text[kMaxLogMessageLen];
    };

    bool enqueueLogEvent(const LogEvent& event);
    void drainLogQueue();
    void drainRxRing();
    void handleRxByte(uint8_t byte);
    void dispatchLine();
    void emitStats();
    void formatUptime(char* out, size_t outSize) const;
    bool transmit(const uint8_t* data, uint16_t len);
    bool transmitLine(const char* text);
    static const char* levelTag(Level level);

    // Message queue for log events produced by any task (drop-oldest overflow policy).
    osMessageQueueId_t logQueueHandle_;
    StaticQueue_t logQueueCb_;
    uint8_t logQueueStorage_[kLogQueueDepth * sizeof(LogEvent)];

    // Single-producer(ISR)/single-consumer(task) byte ring buffer for USB RX; lock-free by construction.
    volatile uint8_t rxRing_[kRxRingSize];
    volatile uint32_t rxHead_;
    volatile uint32_t rxTail_;

    char rxLine_[kMaxRxLineLen];
    uint32_t rxLineLen_;
    bool rxLineTruncated_;

    char txBuffer_[kMaxLogMessageLen + kMaxRxLineLen + 32];

    uint32_t sentCount_;
    uint32_t droppedCount_;
    uint32_t busyDropCount_;
    uint32_t rxOverflowCount_;
    uint32_t rxTruncatedCount_;
};

#endif /* INC_DEBUG_DEBUGSERVICE_HPP_ */
