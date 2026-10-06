#pragma once

#include <cstdint>

namespace Display {

// Sets each refresh frame's length so a display board's timeline follows the
// host's. Pure; PcbDisplayBoard's refresh interrupt calls nextFrame() at
// every frame start, and TimelineSync's decisions set the rate and the phase.
//
// Two corrections add up:
// - the rate: this board's clock error against the host, in ppm. A board
//   running fast (positive) needs more of its own microseconds per frame.
//   Applied a whole microsecond at a time, 1 us per 50 ppm of a 20 ms frame,
//   with the remainder carried, so any rate comes out exact on average.
// - the phase: how far the board's timeline is behind the host's (positive)
//   or ahead (negative). Taken out a little each frame rather than in one
//   jump, so the pass lengths, and so the brightness, change only slightly.
class TimelineServo {
public:
    // Per frame: 0.5 % normally, 5 % while more than 5 ms out (just after
    // a jump; see TimelineSync).
    static constexpr int32_t kSlowSlewMicros = 100;
    static constexpr int32_t kFastSlewMicros = 1000;
    static constexpr int32_t kFastAboveMicros = 5000;
    static constexpr int32_t kMaxRatePpm = 20000;
    static constexpr int32_t kPpmPerMicro = 50;  // 1 us of a 20 ms frame

    void setRatePpm(int32_t ppm)
    {
        rate_ = (ppm > kMaxRatePpm) ? kMaxRatePpm : ((ppm < -kMaxRatePpm) ? -kMaxRatePpm : ppm);
    }
    int32_t ratePpm() const { return rate_; }

    void setPending(int32_t micros) { pending_ = micros; }
    int32_t pending() const { return pending_; }

    // Microseconds to add to the frame that starts now; negative shortens it.
    int32_t nextFrame()
    {
        accumulator_ += rate_;
        const int32_t rateMicros = accumulator_ / kPpmPerMicro;
        accumulator_ -= rateMicros * kPpmPerMicro;

        const int32_t magnitude = (pending_ < 0) ? -pending_ : pending_;
        const int32_t limit = (magnitude > kFastAboveMicros) ? kFastSlewMicros : kSlowSlewMicros;
        const int32_t slew = (pending_ > limit) ? limit : ((pending_ < -limit) ? -limit : pending_);
        pending_ -= slew;
        // Behind the host: a shorter frame catches up.
        return rateMicros - slew;
    }

private:
    int32_t rate_ = 0;
    int32_t pending_ = 0;
    int32_t accumulator_ = 0;
};

} // namespace Display
