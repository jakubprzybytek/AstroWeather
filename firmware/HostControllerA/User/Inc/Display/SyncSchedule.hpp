#pragma once

#include <array>
#include <cstdint>

namespace Display {

// When the host broadcasts its timeline sync and polls the display boards
// for theirs. Pure; DisplaySyncTask drives it with the kernel tick (ms).
//
// - A burst of four syncs, at 0, 10, 30 and 60 s, at boot and whenever a
//   board asks for syncs (or 'time sync now' does): the first sets the
//   board's phase, the later ones measure its rate and pick its HSITRIM.
// - Then one every 5 minutes, counted from the burst's last.
// - A poll of every board's sync status 15 s after boot and then every
//   minute; a board that wants syncs starts a burst. A request during a
//   burst is ignored: the burst serves it.
class SyncSchedule {
public:
    static constexpr std::array<uint32_t, 4> kBurstOffsetsMs = {0U, 10000U, 30000U, 60000U};
    static constexpr uint32_t kIntervalMs = 300000U;
    static constexpr uint32_t kFirstPollMs = 15000U;
    static constexpr uint32_t kPollIntervalMs = 60000U;

    void start(uint32_t now)
    {
        pollAt_ = now + kFirstPollMs;
        startBurst(now);
    }

    // False, and nothing changes, during a burst.
    bool requestBurst(uint32_t now)
    {
        if (inBurst()) {
            return false;
        }
        startBurst(now);
        return true;
    }

    bool inBurst() const { return burstIndex_ < kBurstOffsetsMs.size(); }

    bool syncDue(uint32_t now) const { return reached(now, syncAt_); }
    void onSyncSent(uint32_t now)
    {
        if (inBurst()) {
            ++burstIndex_;
            syncAt_ = burstStart_ + (inBurst() ? kBurstOffsetsMs[burstIndex_]
                                               : kBurstOffsetsMs.back() + kIntervalMs);
            return;
        }
        // Keep the cadence; after a long stall, count from now.
        syncAt_ += kIntervalMs;
        if (reached(now, syncAt_)) {
            syncAt_ = now + kIntervalMs;
        }
    }

    bool pollDue(uint32_t now) const { return reached(now, pollAt_); }
    void onPolled(uint32_t now) { pollAt_ = now + kPollIntervalMs; }

    // Until the next sync or poll; 0 if one is due.
    uint32_t waitMs(uint32_t now) const
    {
        const uint32_t toSync = remaining(now, syncAt_);
        const uint32_t toPoll = remaining(now, pollAt_);
        return (toSync < toPoll) ? toSync : toPoll;
    }
    uint32_t untilSyncMs(uint32_t now) const { return remaining(now, syncAt_); }

private:
    static bool reached(uint32_t now, uint32_t at)
    {
        return static_cast<int32_t>(now - at) >= 0;
    }
    static uint32_t remaining(uint32_t now, uint32_t at)
    {
        return reached(now, at) ? 0U : (at - now);
    }

    void startBurst(uint32_t now)
    {
        burstStart_ = now;
        burstIndex_ = 0U;
        syncAt_ = now;
    }

    uint32_t burstStart_ = 0U;
    std::size_t burstIndex_ = kBurstOffsetsMs.size();
    uint32_t syncAt_ = 0U;
    uint32_t pollAt_ = 0U;
};

} // namespace Display
