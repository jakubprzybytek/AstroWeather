#pragma once

#include <cstdint>

// When the scheduled astro refresh runs. Pure arithmetic, so it runs in the
// native tests; AstroDataRefreshTask reads the clock and asks it. See
// docs/Astro_Data_Refresh_Implementation_Plan.md, section 11.
//
// Refreshes run in fixed local-time slots, 10 minutes after the server's
// Clear Outside ingestion (sst.config.ts, 00:00/06:00/12:00/18:00
// Europe/Warsaw). The 12:10 slot also picks up the noon rollover to the next
// observing night.
//
// A slot is done when a refresh from any source succeeded at or after its
// start. So only the time of the last success is kept, a slot missed while the
// board was off or offline is caught up once, and a manual refresh counts.
// Failures are retried with a growing delay until the slot is done or the next
// one starts.
//
// Wall-clock times are seconds since 2000-01-01 00:00 local
// (Calendar::secondsSince2000()); ticks are milliseconds from
// osKernelGetTickCount().
namespace RefreshSchedule {

constexpr uint32_t kSlotIntervalSeconds = 6U * 3600U;
constexpr uint32_t kSlotOffsetSeconds = 10U * 60U;  // slots at 00:10, 06:10, 12:10, 18:10

// Delay before each retry after consecutive failures; the last repeats.
constexpr uint32_t kRetryDelaysMs[] = {2U * 60000U, 5U * 60000U, 10U * 60000U,
                                       20U * 60000U, 30U * 60000U};
constexpr uint32_t kRetryDelayCount = sizeof(kRetryDelaysMs) / sizeof(kRetryDelaysMs[0]);

constexpr uint32_t retryDelayMs(uint32_t failures)
{
    if (failures == 0U) {
        return 0U;
    }
    return kRetryDelaysMs[(failures <= kRetryDelayCount) ? failures - 1U : kRetryDelayCount - 1U];
}

// Start of the latest slot at or before `now`. Before the first slot of
// 2000-01-01 there is none; 0 stands in for it.
constexpr uint32_t slotStart(uint32_t now)
{
    if (now < kSlotOffsetSeconds) {
        return 0U;
    }
    return now - ((now - kSlotOffsetSeconds) % kSlotIntervalSeconds);
}

constexpr uint32_t nextSlotStart(uint32_t now)
{
    return (now < kSlotOffsetSeconds) ? kSlotOffsetSeconds
                                      : slotStart(now) + kSlotIntervalSeconds;
}

// What the scheduler knows about the clock when it is asked.
struct Clock
{
    bool timeSet;     // false after a power-up, until a refresh sets the RTC
    uint32_t now;     // wall clock; meaningless unless timeSet
    uint32_t tick;
};

class Scheduler
{
public:
    // A success remembered from before a reset.
    void restoreLastSuccess(uint32_t seconds)
    {
        hasSuccess_ = true;
        lastSuccess_ = seconds;
    }

    // Called after every refresh, whatever started it. `now` must be read
    // after the refresh, since a success may have stepped the clock. Without
    // `worthRetrying` (no WiFi credentials) the slot is not retried: it waits
    // for the next slot, or for 'wifi set', which refreshes by itself.
    void recordOutcome(const Clock& clock, bool success, bool worthRetrying)
    {
        if (success && clock.timeSet) {
            hasSuccess_ = true;
            lastSuccess_ = clock.now;
            failures_ = 0U;
            return;
        }
        if (success) {
            // Cannot happen: a success sets the clock. Treat it as done for now.
            failures_ = 0U;
            return;
        }
        if (failures_ < UINT32_MAX) {
            ++failures_;
        }
        retryAtTick_ = clock.tick + retryDelayMs(failures_);
        failedTimeSet_ = clock.timeSet;
        failedSlot_ = clock.timeSet ? slotStart(clock.now) : 0U;
        giveUpUntilNextSlot_ = !worthRetrying;
    }

    bool due(const Clock& clock) const
    {
        if (clock.timeSet && hasSuccess_ && lastSuccess_ >= slotStart(clock.now)) {
            // Also covers a clock stepped back to before the last success.
            return false;
        }
        if (failures_ == 0U) {
            return true;
        }
        // The failures were for an earlier slot: this one starts afresh.
        if (clock.timeSet && failedTimeSet_ && slotStart(clock.now) != failedSlot_) {
            return true;
        }
        if (giveUpUntilNextSlot_) {
            return false;
        }
        return static_cast<int32_t>(clock.tick - retryAtTick_) >= 0;
    }

    bool hasSuccess() const { return hasSuccess_; }
    uint32_t lastSuccess() const { return lastSuccess_; }
    // Consecutive failures since the last success.
    uint32_t failures() const { return failures_; }
    uint32_t retryAtTick() const { return retryAtTick_; }
    bool waitingForNextSlot() const { return failures_ != 0U && giveUpUntilNextSlot_; }

private:
    bool hasSuccess_ = false;
    uint32_t lastSuccess_ = 0U;
    uint32_t failures_ = 0U;
    uint32_t retryAtTick_ = 0U;
    bool failedTimeSet_ = false;
    uint32_t failedSlot_ = 0U;
    bool giveUpUntilNextSlot_ = false;
};

}  // namespace RefreshSchedule
