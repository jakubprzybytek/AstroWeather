#pragma once

#include <Display/Timeline.hpp>

#include <cstdint>

namespace Display {

// A display board's decisions on each sync from the host. Pure; the board's
// task feeds it the host's position and its own at the instant the message
// arrived, and applies the decision to TimelineServo, the frame numbers and
// HSITRIM.
//
// - The first sync, or one further out than drift could take the board
//   (50 ms plus 2.5 % of the time since the previous sync: the host
//   rebooted), jumps: the frames are renumbered to the nearest whole frame
//   and the rest, at most half a frame, is slewed out.
// - Later syncs measure the drift since the previous one: the error now,
//   less what the servo had still to correct when the message arrived.
//   Divided by the time between the syncs, it corrects the rate, so the
//   board follows the host's clock and the next sync finds little to fix.
// - That rate is the average over the last interval, which lags a clock
//   drifting with temperature by half an interval. So when two measured
//   intervals in a row are each 2 minutes or more, the change between their
//   rates is carried on over the next interval (at most 500 ppm). Shorter
//   intervals, a burst's, give rates too noisy to extrapolate: on the bench
//   they were 40-160 ppm off the 5-minute ones (2026-10-06). At 400 ppm an
//   hour, the 2026-10-05 bench drift, a 5-minute interval ends about 10 ms
//   out without it; with it, about 1.5 ms at most, mid-interval, where a
//   constant rate bows away from the drifting clock.
// - A rate more than 2000 ppm out suggests a whole number of HSITRIM steps
//   (about 0.33 % each, measured on a G070 on 2026-10-05) to bring the
//   board's own clock near the host's; the servo takes the rest.
//
// locked() is what the board reports when the host polls it: false asks the
// host for a burst of syncs (DisplaySyncTask). It is true once a sync has
// measured the rate and found the timeline within 10 ms, and false again
// after a jump, an HSITRIM step or a sync further out.
class TimelineSync {
public:
    static constexpr int64_t kJumpAboveMicros = 50000;
    // Plus this share of the interval: more than any clock the servo can follow.
    static constexpr int64_t kJumpIntervalDivisor = 40;
    static constexpr int64_t kLockedWithinMicros = 10000;
    // Shorter intervals give too coarse a rate; such a sync corrects only the phase.
    static constexpr int64_t kMinRateIntervalMicros = 5000000;
    static constexpr int32_t kHsiStepPpm = 3300;
    static constexpr int32_t kHsiStepAbovePpm = 2000;
    static constexpr int32_t kMaxRatePpm = 20000;
    static constexpr int64_t kMinTrendIntervalMicros = 120000000;
    static constexpr int64_t kMaxTrendPpm = 500;

    struct Decision {
        int32_t jumpFrames = 0;    // renumber the frames by this much
        int32_t phaseMicros = 0;   // correction to make, counted from the stamp
        int32_t ratePpm = 0;       // the servo's new rate
        int32_t driftMicros = 0;   // drift since the previous sync, if rateMeasured
        int32_t errorMicros = 0;   // host minus board at the stamp, before any jump
        int8_t hsiSteps = 0;       // suggested HSITRIM change; 0 for none
        bool jumped = false;
        bool rateMeasured = false;
    };

    // `hostMicros` is the host's position when the message arrived (its
    // stamp plus the transfer time), `boardMicros` the board's, and
    // `pendingMicros` what the servo still had to correct then.
    Decision onSync(int64_t hostMicros, int64_t boardMicros, int32_t pendingMicros,
                    int32_t ratePpm)
    {
        Decision decision{};
        const int64_t error = hostMicros - boardMicros;
        decision.errorMicros = clamp32(error);
        decision.ratePpm = ratePpm;
        ++syncs_;

        const int64_t interval = hostMicros - previousHost_;
        const int64_t jumpAbove =
            kJumpAboveMicros + ((interval > 0) ? interval / kJumpIntervalDivisor : 0);
        if (!hasPrevious_ || error > jumpAbove || error < -jumpAbove) {
            const int64_t frame = static_cast<int64_t>(kFrameMicros);
            const int64_t frames = (error >= 0) ? (error + frame / 2) / frame
                                                : -((-error + frame / 2) / frame);
            decision.jumpFrames = static_cast<int32_t>(frames);
            decision.phaseMicros = static_cast<int32_t>(error - frames * frame);
            decision.jumped = true;
            hasPrevious_ = true;
            previousHost_ = hostMicros;
            hasAverage_ = false;
            locked_ = false;
            ++jumps_;
            return decision;
        }

        decision.phaseMicros = clamp32(error);
        previousHost_ = hostMicros;
        if (interval >= kMinRateIntervalMicros) {
            const int64_t drift = error - pendingMicros;
            // The servo ran at ratePpm all interval; this is what the clock averaged.
            const int64_t average = static_cast<int64_t>(ratePpm) - drift * 1000000 / interval;
            int64_t trend = 0;
            if (hasAverage_ && interval >= kMinTrendIntervalMicros &&
                previousInterval_ >= kMinTrendIntervalMicros) {
                // The two averages lie (interval + previous) / 2 apart; carry
                // that slope on for another interval like this one.
                trend = (average - previousAverage_) * interval * 2 / (interval + previousInterval_);
                trend = (trend > kMaxTrendPpm) ? kMaxTrendPpm
                                               : ((trend < -kMaxTrendPpm) ? -kMaxTrendPpm : trend);
            }
            hasAverage_ = true;
            previousAverage_ = average;
            previousInterval_ = interval;
            const int64_t rate = average + trend;
            decision.driftMicros = clamp32(drift);
            decision.ratePpm = static_cast<int32_t>(
                (rate > kMaxRatePpm) ? kMaxRatePpm : ((rate < -kMaxRatePpm) ? -kMaxRatePpm : rate));
            decision.rateMeasured = true;
            if (decision.ratePpm > kHsiStepAbovePpm || decision.ratePpm < -kHsiStepAbovePpm) {
                // Slow (negative) wants a higher trim. Rounded to the nearest step.
                const int32_t steps = (decision.ratePpm >= 0)
                                          ? -((decision.ratePpm + kHsiStepPpm / 2) / kHsiStepPpm)
                                          : ((-decision.ratePpm + kHsiStepPpm / 2) / kHsiStepPpm);
                decision.hsiSteps = static_cast<int8_t>(steps);
            }
        }
        const int64_t magnitude = (error < 0) ? -error : error;
        locked_ = (decision.rateMeasured || locked_) && magnitude <= kLockedWithinMicros;
        return decision;
    }

    // HSITRIM has moved by `steps`: the clock is that many steps faster, so
    // the servo needs that much more rate. Unlocks until a sync confirms it.
    int32_t afterHsiSteps(int32_t ratePpm, int32_t steps)
    {
        locked_ = false;
        // The averages before the step no longer compare with those after.
        hasAverage_ = false;
        ++hsiChanges_;
        return ratePpm + steps * kHsiStepPpm;
    }

    bool locked() const { return locked_; }
    uint32_t syncs() const { return syncs_; }
    uint32_t jumps() const { return jumps_; }
    uint32_t hsiChanges() const { return hsiChanges_; }

private:
    static int32_t clamp32(int64_t value)
    {
        return static_cast<int32_t>((value > INT32_MAX) ? INT32_MAX
                                                        : ((value < INT32_MIN) ? INT32_MIN : value));
    }

    bool hasPrevious_ = false;
    bool locked_ = false;
    bool hasAverage_ = false;
    int64_t previousHost_ = 0;
    int64_t previousAverage_ = 0;
    int64_t previousInterval_ = 0;
    uint32_t syncs_ = 0U;
    uint32_t jumps_ = 0U;
    uint32_t hsiChanges_ = 0U;
};

} // namespace Display
