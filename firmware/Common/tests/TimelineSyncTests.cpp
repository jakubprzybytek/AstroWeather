#include <Device/HsiTrim.hpp>
#include <Display/Timeline.hpp>
#include <Display/TimelineServo.hpp>
#include <Display/TimelineSync.hpp>

#include <Expect.hpp>

#include <cmath>
#include <cstdio>
#include <string>
#include <vector>

// The display board's side of the timeline sync: the servo that sets each
// frame's length, the decisions on each sync, and the two together against
// a simulated host on the real schedule.

namespace {

using Test::expect;
using Test::expectEqual;

void testServoRate()
{
    Display::TimelineServo servo;
    servo.setRatePpm(-4800);
    int64_t total = 0;
    for (int frame = 0; frame < 1000; ++frame) {
        total += servo.nextFrame();
    }
    // -4800 ppm of 1000 frames of 20 ms: -96 us a frame.
    expectEqual(total, static_cast<int64_t>(-96000), "rate sums exactly");

    servo.setRatePpm(30);
    total = 0;
    for (int frame = 0; frame < 5000; ++frame) {
        total += servo.nextFrame();
    }
    // 30 ppm of 100 s is 3000 us, a microsecond every 1.67 frames.
    expectEqual(total, static_cast<int64_t>(3000), "a fraction of a microsecond a frame adds up");

    servo.setRatePpm(50000);
    expectEqual(servo.ratePpm(), Display::TimelineServo::kMaxRatePpm, "rate clamped");
}

void testServoSlew()
{
    Display::TimelineServo servo;
    servo.setPending(250);
    expectEqual(servo.nextFrame(), -100, "behind: a shorter frame");
    expectEqual(servo.nextFrame(), -100, "at most 100 us a frame");
    expectEqual(servo.nextFrame(), -50, "the rest");
    expectEqual(servo.nextFrame(), 0, "done");
    expectEqual(servo.pending(), 0, "nothing left");

    servo.setPending(-8000);
    expectEqual(servo.nextFrame(), 1000, "ahead and far out: 1 ms longer");
    for (int frame = 0; frame < 2; ++frame) {
        servo.nextFrame();
    }
    expectEqual(servo.pending(), -5000, "fast down to 5 ms");
    expectEqual(servo.nextFrame(), 100, "then slow");
}

void testFirstSyncJumps()
{
    Display::TimelineSync sync;
    // The host is 1.2345 s ahead: 62 frames less 5.5 ms.
    const auto decision = sync.onSync(10000000 + 1234500, 10000000, 0, 0);
    expect(decision.jumped, "first sync jumps");
    expectEqual(decision.jumpFrames, 62, "to the nearest frame");
    expectEqual(decision.phaseMicros, -5500, "the rest slewed");
    expect(!decision.rateMeasured, "no rate from one sync");
    expect(!sync.locked(), "not locked");

    const auto behind = Display::TimelineSync{}.onSync(0, 1234500, 0, 0);
    expectEqual(behind.jumpFrames, -62, "board ahead: frames back");
    expectEqual(behind.phaseMicros, 5500, "and the rest");
}

void testRateAndHsiSuggestion()
{
    Display::TimelineSync sync;
    sync.onSync(1000000, 1000000, 0, 0);
    // 10 s later the board is 48 ms behind: 4800 ppm slow.
    const auto decision = sync.onSync(11000000, 11000000 - 48000, 0, 0);
    expect(!decision.jumped, "48 ms is drift, not a jump");
    expect(decision.rateMeasured, "rate measured");
    expectEqual(decision.ratePpm, -4800, "rate");
    expectEqual(decision.driftMicros, 48000, "drift");
    expectEqual(decision.phaseMicros, 48000, "phase to correct");
    expectEqual(static_cast<int>(decision.hsiSteps), 1, "one step faster");
    expectEqual(sync.afterHsiSteps(decision.ratePpm, 1), -1500, "the servo keeps the rest");
    expect(!sync.locked(), "an HSITRIM step unlocks");

    // What the servo still had to correct at the stamp is not drift.
    Display::TimelineSync pending;
    pending.onSync(0, 0, 0, 0);
    const auto withPending = pending.onSync(10000000, 10000000 - 3000, 3000, -1000);
    expectEqual(withPending.driftMicros, 0, "pending correction is not drift");
    expectEqual(withPending.ratePpm, -1000, "rate unchanged");
    expectEqual(static_cast<int>(withPending.hsiSteps), 0, "no step within 2000 ppm");
    expect(pending.locked(), "locked within 10 ms with a rate");

    Display::TimelineSync quick;
    quick.onSync(0, 0, 0, 0);
    const auto soon = quick.onSync(2000000, 2000000 - 400, 0, 0);
    expect(!soon.rateMeasured, "under 5 s: phase only");
    expectEqual(soon.phaseMicros, 400, "phase still corrected");
}

void testLaterJump()
{
    Display::TimelineSync sync;
    sync.onSync(0, 0, 0, 0);
    sync.onSync(10000000, 10000000, 0, 0);
    expect(sync.locked(), "locked");
    // The host rebooted: its frames start again from 1.
    const auto decision = sync.onSync(500000, 20000000, 0, 0);
    expectEqual(decision.jumpFrames, -975, "renumbered");
    expect(decision.jumped, "host reboot jumps");
    expect(!sync.locked(), "and unlocks");
    expectEqual(sync.jumps(), 2U, "two jumps");
}

void testAllowedSteps()
{
    // The G070 on the bench: HSICAL 0x8F at trim 64.
    expectEqual(HsiTrim::allowedSteps(0x8F, 64, 1, 64, 8), 1, "a step up");
    expectEqual(HsiTrim::allowedSteps(0x8F, 64, -3, 64, 8), -3, "steps down");
    expectEqual(HsiTrim::allowedSteps(0x8F, 70, 5, 64, 8), 2, "limited to 8 from boot");
    expectEqual(HsiTrim::allowedSteps(0x82, 64, -5, 64, 8), -2, "stops short of HSICAL 0x7F");
    expectEqual(HsiTrim::allowedSteps(0xBE, 64, 3, 64, 8), 1, "stops short of HSICAL 0xC0");
    expectEqual(HsiTrim::allowedSteps(0x80, 64, -1, 64, 8), 0, "none at the band edge");
}

// One display board against the host, frame by frame, on the host's real
// schedule: a burst at 0, 10, 30 and 60 s, then every 5 min. The board's
// clock is `errorPpm` slow or fast and drifts `driftPpmPerHour`; HSITRIM
// steps it by `stepPpm`. Returns the worst offset after the first
// `settleSeconds`, in us; `steps` gets the HSITRIM steps taken.
struct SimulationResult {
    double worstMicros;
    int steps;
    bool locked;
    int32_t worstAdjust;
};

SimulationResult simulate(double errorPpm, double driftPpmPerHour, double stepPpm,
                          double minutes, double settleSeconds, double bootOffsetMicros)
{
    Display::TimelineServo servo;
    Display::TimelineSync sync;
    const double frameMicros = Display::kFrameMicros;

    std::vector<double> syncTimes = {0.0, 10e6, 30e6, 60e6};
    for (double at = 60e6 + 300e6; at < minutes * 60e6; at += 300e6) {
        syncTimes.push_back(at);
    }
    // Real time is the host's clock; the board's first frame starts at
    // bootOffsetMicros, so the first sync finds it far out.
    const double start = 1e6;
    double frameStart = start;
    uint32_t frame = 0U;
    int32_t adjust = servo.nextFrame();
    int trim = 64;
    int steps = 0;
    std::size_t nextSync = 0U;
    SimulationResult result{0.0, 0, false, 0};

    const double end = start + minutes * 60e6;
    while (frameStart < end) {
        const double hours = (frameStart - start) / 3600e6;
        const double rate =
            1.0 + (errorPpm + driftPpmPerHour * hours + (trim - 64) * stepPpm) * 1e-6;
        const double ownMicros = frameMicros + adjust;
        const double frameEnd = frameStart + ownMicros / rate;

        while (nextSync < syncTimes.size() && start + syncTimes[nextSync] < frameEnd) {
            const double at = start + syncTimes[nextSync];
            if (at >= frameStart) {
                const int64_t host = static_cast<int64_t>(at - start + bootOffsetMicros);
                Display::TimelineStamp stamp{};
                stamp.frame = frame;
                stamp.frameStart = 0U;
                stamp.frameMicros = static_cast<uint32_t>(ownMicros);
                stamp.now = static_cast<uint32_t>((at - frameStart) * rate);
                stamp.pendingMicros = servo.pending();
                const auto decision = sync.onSync(host, Display::positionMicros(stamp),
                                                  stamp.pendingMicros, servo.ratePpm());
                int32_t newRate = decision.ratePpm;
                if (decision.hsiSteps != 0) {
                    const int32_t allowed = HsiTrim::allowedSteps(
                        static_cast<uint8_t>(0x8F + trim - 64), static_cast<uint8_t>(trim),
                        decision.hsiSteps, 64, 8);
                    if (allowed != 0) {
                        trim += allowed;
                        steps += allowed;
                        newRate = sync.afterHsiSteps(newRate, allowed);
                    }
                }
                frame = static_cast<uint32_t>(static_cast<int64_t>(frame) + decision.jumpFrames);
                servo.setPending(decision.phaseMicros);
                servo.setRatePpm(newRate);
            }
            ++nextSync;
        }

        frameStart = frameEnd;
        ++frame;
        adjust = servo.nextFrame();
        if (frameStart - start >= settleSeconds * 1e6) {
            const double host = frameStart - start + bootOffsetMicros;
            const double offset = std::fabs(host - static_cast<double>(frame) * frameMicros);
            if (offset > result.worstMicros) {
                result.worstMicros = offset;
            }
            const int32_t magnitude = (adjust < 0) ? -adjust : adjust;
            if (magnitude > result.worstAdjust) {
                result.worstAdjust = magnitude;
            }
        }
    }
    result.steps = steps;
    result.locked = sync.locked();
    std::printf("simulated %+.0f ppm %+.0f ppm/h: worst %.0f us, %d steps, adjust %d us\n",
                errorPpm, driftPpmPerHour, result.worstMicros, steps, result.worstAdjust);
    return result;
}

void testSimulatedBench()
{
    // The 2026-10-05 boards: the display 4800 ppm slow of the host, a real
    // step of 0.338 % where the board assumes 0.33 %.
    const SimulationResult result = simulate(-4800.0, 0.0, 3380.0, 60.0, 90.0, 7654321.0);
    expect(result.worstMicros < 2000.0, "within 2 ms once settled");
    expectEqual(result.steps, 1, "one HSITRIM step");
    expect(result.locked, "locked");
    expect(result.worstAdjust < 100, "frames within 0.5 % once settled");
}

void testSimulatedWorstClock()
{
    // A 1 % spread, the design margin: 100 ms of drift by the second sync.
    const SimulationResult result = simulate(-10000.0, 0.0, 3300.0, 60.0, 150.0, 55555.0);
    expect(result.locked, "a 1 % board locks");
    expectEqual(result.steps, 3, "three HSITRIM steps");
    expect(result.worstMicros < 2000.0, "within 2 ms once settled");
}

void testSimulatedTemperatureDrift()
{
    // The bench saw the display's HSI move ~400 ppm in an hour.
    const SimulationResult result = simulate(-2500.0, 400.0, 3380.0, 120.0, 90.0, 123456.0);
    expect(result.worstMicros < 10000.0, "within 10 ms between 5 min syncs");
    // Once two 5-minute rates give the trend, only the bow of a constant rate
    // against a drifting clock is left: 400 ppm/h * (5 min)^2 / 8, 1.25 ms.
    const SimulationResult steady = simulate(-2500.0, 400.0, 3380.0, 120.0, 900.0, 123456.0);
    expect(steady.worstMicros < 2000.0, "within 2 ms once the trend is known");
    const SimulationResult falling = simulate(1500.0, -400.0, 3380.0, 120.0, 90.0, 9999.0);
    expect(falling.worstMicros < 10000.0, "falling too");
    expectEqual(falling.steps, 0, "1500 ppm needs no HSITRIM step");
}

void testHeartbeat()
{
    expect(Display::heartbeatLit(1U), "frame 1 lit");
    expect(Display::heartbeatLit(101U), "every 100 frames");
    expect(!Display::heartbeatLit(0U) && !Display::heartbeatLit(2U), "one frame only");
}

void testPosition()
{
    Display::TimelineStamp stamp{10U, 1000U, 20000U, 1000U + 5000U, 0};
    expectEqual(Display::positionMicros(stamp), static_cast<int64_t>(205000), "frame and time into it");
    stamp.frameMicros = 19900U;
    stamp.now = 1000U + 9950U;
    expectEqual(Display::positionMicros(stamp), static_cast<int64_t>(210000),
                "a shortened frame still spans 20 ms");
}

} // namespace

int main()
{
    testServoRate();
    testServoSlew();
    testFirstSyncJumps();
    testRateAndHsiSuggestion();
    testLaterJump();
    testAllowedSteps();
    testHeartbeat();
    testPosition();
    testSimulatedBench();
    testSimulatedWorstClock();
    testSimulatedTemperatureDrift();
    return Test::finish("TimelineSync");
}
