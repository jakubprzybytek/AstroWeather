#pragma once

#include <cstdint>

namespace Display {

// The refresh timeline every board keeps in step with the host's: the
// refresh frames, numbered from the host's boot, and the time within each.
// Display blinking and the LED_1 heartbeat both run off the frame number, so
// boards on the same timeline blink together. The host's own refresh is the
// reference; a display board corrects its frame length to follow it (see
// TimelineServo and TimelineSync), and runs free until the first sync.
// firmware/Docs/I2C.md describes the sync message.

constexpr uint32_t kFrameMicros = 20000U;  // five 4 ms slots: 50 Hz
// 2 s: the LED_1 heartbeat period, two display blink periods.
constexpr uint32_t kCycleFrames = 100U;

// LED_1 is lit for the first frame of each cycle: 20 ms every 2 s. Frame 1,
// since the refresh numbers its first frame 1 (RefreshSequencer), so the
// heartbeat starts with the first blink-on phase.
constexpr bool heartbeatLit(uint32_t frame)
{
    return (frame % kCycleFrames) == 1U;
}

// The timeline at one instant, as PcbDisplayBoard::stampNow() reads it.
// Times are MicroClock microseconds of the board's own clock.
struct TimelineStamp {
    uint32_t frame;          // the frame in progress
    uint32_t frameStart;     // when it started
    uint32_t frameMicros;    // its length on this board's clock, corrections included
    uint32_t now;            // the instant stamped
    int32_t pendingMicros;   // phase correction the servo had still to make
};

// Where a stamp falls on the timeline, in nominal microseconds from the start
// of frame 0. The time into the frame is scaled by the frame's length, so a
// corrected frame still spans kFrameMicros.
inline int64_t positionMicros(const TimelineStamp& stamp)
{
    const uint32_t into = stamp.now - stamp.frameStart;
    const uint32_t length = (stamp.frameMicros != 0U) ? stamp.frameMicros : kFrameMicros;
    const int64_t scaled = static_cast<int64_t>(into) * kFrameMicros / length;
    return static_cast<int64_t>(stamp.frame) * kFrameMicros + scaled;
}

} // namespace Display
