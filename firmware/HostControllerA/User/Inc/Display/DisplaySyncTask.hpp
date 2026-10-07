#pragma once

#include <Device/I2cBus.hpp>
#include <Display/BoardChain.hpp>
#include <Display/Display.hpp>
#include <Display/SyncSchedule.hpp>
#include <Utils/Task.hpp>

#include <array>
#include <cstdint>

// Keeps the display boards' refresh timelines on the host's
// (Display/Timeline.hpp): broadcasts the host's position to the I2C
// general-call address on SyncSchedule's timetable, and polls each remote
// board's sync status to start a burst for one that wants it - after a
// reset, a jump, an HSITRIM step or a sync that found it more than 10 ms out.
//
// The host's own refresh is the reference and is never corrected. With no
// board present the broadcast goes unanswered (a NACK); that is not an error.
//
// 2048 B: a burst's log line goes through LogService::logf() and vsnprintf,
// which overflowed 1024 (bench, 2026-10-06).
class DisplaySyncTask : public Task<2048> {
public:
    static DisplaySyncTask& instance();

    // Before start().
    void init(Device::I2cBus* bus, Display::Display* display);

    // A burst now, unless one is running; from any task.
    void requestBurst();

    // For 'time sync'.
    struct Status {
        uint32_t sent;            // broadcasts sent, answered or not
        uint32_t answered;        // of those, taken by at least one board
        uint32_t lastSentTick;    // 0 if none yet
        uint32_t untilNextMs;
        uint32_t bursts;
        bool inBurst;
        // Per chain position: the last poll's answer, 0 if the board did not
        // answer or has no sync; the host's own position stays 0.
        std::array<uint8_t, Display::kChainLength> boardStatus;
    };
    Status status() const;

protected:
    void run() override;

private:
    static constexpr uint32_t kFlagBurst = 1U << 0;
    static constexpr uint32_t kTransferTimeoutMs = 5U;

    DisplaySyncTask();
    void sendSync(uint32_t now);
    void pollBoards(uint32_t now);
    void resendContent(uint8_t position, uint32_t now);
    void startBurst(uint32_t now, const char* reason);

    Device::I2cBus* bus_ = nullptr;
    Display::Display* display_ = nullptr;
    Display::SyncSchedule schedule_;
    volatile uint32_t sent_ = 0U;
    volatile uint32_t answered_ = 0U;
    volatile uint32_t lastSentTick_ = 0U;
    volatile uint32_t untilNextMs_ = 0U;
    volatile uint32_t bursts_ = 0U;
    volatile bool inBurst_ = false;
    std::array<volatile uint8_t, Display::kChainLength> boardStatus_{};
};
