#pragma once

#include <Display/Timeline.hpp>

#include <cstdint>

namespace Display {

// A display board's decisions on each sync from the host. Pure; the board's
// task feeds it the host's position and its own at the instant the message
// arrived, and applies the decision to TimelineServo, the frame numbers and
// HSITRIM.
//
// - The first sync, or one whose error, less what the servo still had to
//   correct, is further out than drift could take the board (50 ms plus
//   2.5 % of the time since the previous sync: the host rebooted), jumps:
//   the frames are renumbered to the nearest whole frame and the rest, at
//   most half a frame, is slewed out.
// - Every later sync corrects the phase by the error it finds.
// - A sync 25 s or more after the previous one also measures the rate: the
//   drift since the previous sync (the error now, less what the servo had
//   still to correct when the message arrived), divided by the time between
//   them, taken off the rate the servo ran at. Each stamp is a millisecond
//   or two out, so a short interval gives only a rough rate: 2 ms over 30 s
//   is 70 ppm, which is 8 ms two minutes later. An interval under 100 s (a
//   burst's) therefore only nudges the rate, weighted by its length against
//   the intervals before it (up to 5 minutes' worth); one of 100 s or more
//   (the host's regular 2 minutes) sets the rate outright, the stamps' error
//   being 17 ppm or less by then. (On the bench on 2026-10-07 every 30 s
//   rate missed the following 5-minute average by 100-160 ppm.)
// - The rate is the average over the last long interval: no trend is carried
//   on from the one before. A clock drifting with temperature at 400 ppm an
//   hour (the 2026-10-05 bench) then ends each 2-minute interval about 2 ms
//   out, which a trend would halve; but on the bench the rate of one board
//   against the other moves in steps as often as in lines (100-300 ppm
//   between one 5-minute average and the next, both ways, 2026-10-07), and a
//   trend doubles a step: 90 ms twice, where the step alone cost 90 ms once.
// - A rate more than 2500 ppm out suggests a whole number of HSITRIM steps
//   (about 0.33 % each, measured on a G070 on 2026-10-05) to bring the
//   board's own clock near the host's; the servo takes the rest, at most
//   1650 ppm after a step, so the clock has to wander 850 ppm before a step
//   is undone. (Stepping above 2000 ppm, the bench board's trim moved 8
//   times in 90 minutes.)
//
// locked() is what the board reports when the host polls it: false asks the
// host for a burst of syncs (DisplaySyncTask). It is true once a sync has
// measured the rate, and false again after a jump or an HSITRIM step, when
// the rate is unknown or has just changed. A sync that finds a large error
// corrects it and measures the rate over its whole interval; a burst's short
// intervals could only measure it worse.
class TimelineSync {
public:
    static constexpr int64_t kJumpAboveMicros = 50000;
    // Plus this share of the interval: more than any clock the servo can follow.
    static constexpr int64_t kJumpIntervalDivisor = 40;
    // Shorter intervals give too rough a rate; such a sync corrects only the
    // phase. Under a burst's 30 s, which arrives a little early or late.
    static constexpr int64_t kMinRateIntervalMicros = 25000000;
    // An interval this long sets the rate; a shorter one nudges it. Under the
    // host's regular 2 minutes, which arrive a little early or late.
    static constexpr int64_t kLongIntervalMicros = 100000000;
    // How much earlier measurement a short interval is weighed against, at most.
    static constexpr int64_t kRateMemoryMicros = 300000000;
    static constexpr int32_t kHsiStepPpm = 3300;
    static constexpr int32_t kHsiStepAbovePpm = 2500;
    static constexpr int32_t kMaxRatePpm = 20000;

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
        // What the servo still had to correct is expected; only the rest can
        // be a jump. A sync just after one that found a large error sees
        // most of it still pending.
        const int64_t drift = error - pendingMicros;
        if (!hasPrevious_ || drift > jumpAbove || drift < -jumpAbove) {
            const int64_t frame = static_cast<int64_t>(kFrameMicros);
            const int64_t frames = (error >= 0) ? (error + frame / 2) / frame
                                                : -((-error + frame / 2) / frame);
            decision.jumpFrames = static_cast<int32_t>(frames);
            decision.phaseMicros = static_cast<int32_t>(error - frames * frame);
            decision.jumped = true;
            hasPrevious_ = true;
            previousHost_ = hostMicros;
            weight_ = 0;
            locked_ = false;
            ++jumps_;
            return decision;
        }

        decision.phaseMicros = clamp32(error);
        previousHost_ = hostMicros;
        if (interval < kMinRateIntervalMicros) {
            return decision;
        }

        // The servo ran at ratePpm all interval; this is what the clock averaged.
        const int64_t average = static_cast<int64_t>(ratePpm) - drift * 1000000 / interval;
        int64_t rate = average;
        if (interval >= kLongIntervalMicros) {
            weight_ = (interval < kRateMemoryMicros) ? interval : kRateMemoryMicros;
        } else {
            const int64_t weight = (weight_ < kRateMemoryMicros) ? weight_ : kRateMemoryMicros;
            rate = (static_cast<int64_t>(ratePpm) * weight + average * interval) /
                   (weight + interval);
            weight_ = weight + interval;
        }
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
        locked_ = true;
        return decision;
    }

    // HSITRIM has moved by `steps`: the clock is that many steps faster, so
    // the servo needs that much more rate. Unlocks until a sync confirms it.
    int32_t afterHsiSteps(int32_t ratePpm, int32_t steps)
    {
        locked_ = false;
        // The rate is now an assumption (a step is 0.338 % on the bench
        // board, not 0.33 %): the next measurement replaces it.
        weight_ = 0;
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
    int64_t previousHost_ = 0;
    int64_t weight_ = 0;  // how much measured interval the rate rests on
    uint32_t syncs_ = 0U;
    uint32_t jumps_ = 0U;
    uint32_t hsiChanges_ = 0U;
};

} // namespace Display
