#include <TimelineFollower.hpp>

#include <Device/HsiTrim.hpp>
#include <Display/DisplayI2cProtocol.hpp>
#include <Display/Timeline.hpp>
#include <Stats.hpp>

TimelineFollower::TimelineFollower(Display::PcbDisplayBoard& board, I2cTarget& link)
    : board_(board), link_(link)
{
}

void TimelineFollower::init()
{
    bootTrim_ = HsiTrim::trim();
    g_displayStats.hsiTrim = bootTrim_;
    link_.setStatus(Display::kSyncStatusWanted);
}

void TimelineFollower::onSync(const I2cTarget::Sync& sync)
{
    uint32_t frame = 0U;
    uint16_t micros = 0U;
    if (!Display::deserializeSync(sync.message.data(), sync.message.size(), frame, micros)) {
        ++g_displayStats.syncsRejected;
        return;
    }

    // Where the host was when the message arrived, and where this board was.
    const int64_t host = static_cast<int64_t>(frame) * Display::kFrameMicros + micros +
                         Display::kSyncTransferMicros;
    const int64_t board = Display::positionMicros(sync.stamp);
    const Display::TimelineSync::Decision decision =
        sync_.onSync(host, board, sync.stamp.pendingMicros, board_.ratePpm());

    int32_t rate = decision.ratePpm;
    if (decision.hsiSteps != 0) {
        const uint8_t trim = HsiTrim::trim();
        const int32_t steps = HsiTrim::allowedSteps(HsiTrim::calibration(), trim,
                                                    decision.hsiSteps, bootTrim_, kHsiLimit);
        if (steps != 0 && HsiTrim::set(static_cast<uint8_t>(trim + steps))) {
            rate = sync_.afterHsiSteps(rate, steps);
        }
    }
    board_.applySync(decision.jumpFrames, decision.phaseMicros, sync.stamp.pendingMicros, rate);
    link_.setStatus(sync_.locked() ? Display::kSyncStatusLocked : Display::kSyncStatusWanted);

    g_displayStats.syncsApplied = sync_.syncs();
    g_displayStats.syncJumps = sync_.jumps();
    g_displayStats.syncLocked = sync_.locked() ? 1U : 0U;
    g_displayStats.syncErrorMicros = static_cast<uint32_t>(decision.errorMicros);
    if (decision.rateMeasured) {
        g_displayStats.syncDriftMicros = static_cast<uint32_t>(decision.driftMicros);
    }
    g_displayStats.syncRatePpm = static_cast<uint32_t>(rate);
    g_displayStats.hsiTrim = HsiTrim::trim();
    g_displayStats.hsiChanges = sync_.hsiChanges();
}
