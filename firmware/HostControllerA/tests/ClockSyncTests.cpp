#include <HostController/ClockSync.hpp>

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char* caseName)
{
    if (!condition) {
        std::cerr << caseName << " failed\n";
        ++failures;
    }
}

constexpr int64_t kHourMs = 3600000;
constexpr int32_t kSecondsError = ClockSync::kWholeSeconds.sampleErrorMs;
constexpr int32_t kMillisError = ClockSync::kMilliseconds.sampleErrorMs;

void testArithmetic()
{
    expect(ClockSync::ppmOf(3600, kHourMs) == 1000, "3.6 s an hour is 1000 ppm");
    expect(ClockSync::ppmOf(-3600, kHourMs) == -1000, "negative drift keeps its sign");
    expect(ClockSync::ppmOf(1, 0) == 0, "an empty span gives 0");
    expect(ClockSync::uncertaintyPpm(kHourMs, 600, 600) == 333,
           "whole seconds: one hour is good to 333 ppm");
    expect(ClockSync::uncertaintyPpm(kHourMs, 150, 150) == 83,
           "milliseconds: one hour is good to 83 ppm");
    expect(ClockSync::combinedTrimPpm(18400, 0) == 18400, "no drift keeps the trim");
    // (1.0184)(1.000798) - 1 = 0.0192126832, as tools/rtc_offset.py suggested
    expect(ClockSync::combinedTrimPpm(18400, 798) == 19213, "the overnight run's trim");
    expect(ClockSync::combinedTrimPpm(0, -500) == -500, "untrimmed drift is the trim");
    expect(ClockSync::lsiMilliHz(0, 0) == 32000000U, "nominal LSI");
    expect(ClockSync::lsiMilliHz(18400, 798) == 32614816U, "32 614.816 Hz");
}

void testFirstSyncStartsTheMeasurement()
{
    ClockSync::DriftTracker tracker;
    const ClockSync::Result result = tracker.record(1000000, 2500, true, kSecondsError);
    expect(result.kind == ClockSync::Kind::First, "first sync");
    expect(!result.sinceLast.valid && !result.sinceReference.valid, "no spans yet");
    expect(result.referenceMs == 1000000, "reference is the first sync");
}

void testDriftAcrossKeptAndSteppedSyncs()
{
    ClockSync::DriftTracker tracker;
    // Starts on time, gains 300 ms an hour (83 ppm).
    (void)tracker.record(0, 0, true, kSecondsError);
    ClockSync::Result result = tracker.record(kHourMs, 300, false, kSecondsError);
    expect(result.kind == ClockSync::Kind::Drift, "second sync measures drift");
    expect(result.sinceLast.driftMs == 300, "first hour drift");
    expect(result.sinceReference.driftMs == 300, "total after one hour");

    // Kept, so the next offset still includes the first hour's 300 ms.
    result = tracker.record(2 * kHourMs, 600, false, kSecondsError);
    expect(result.sinceLast.driftMs == 300, "second hour drift excludes the first");
    expect(result.sinceReference.driftMs == 600, "total after two hours");

    // Enough to step; afterwards the RTC is back on time.
    result = tracker.record(4 * kHourMs, 1200, true, kSecondsError);
    expect(result.sinceLast.driftMs == 600, "two hours before the step");
    expect(result.sinceReference.driftMs == 1200, "total at the step");

    result = tracker.record(8 * kHourMs, 1200, false, kSecondsError);
    expect(result.sinceLast.driftMs == 1200, "drift after the step starts from zero");
    expect(result.sinceReference.driftMs == 2400, "the step does not lose drift");
    expect(result.sinceReference.spanMs == 8 * kHourMs, "span from the reference");
    expect(result.sinceReference.ppm == 83, "83 ppm overall");
    expect(result.sinceReference.uncertaintyPpm == 42, "eight hours: +-42 ppm");
}

void testStepRestartsTheMeasurement()
{
    ClockSync::DriftTracker tracker;
    (void)tracker.record(0, 0, true, kSecondsError);
    // An hour later the RTC is an hour out: a DST change, not drift.
    ClockSync::Result result = tracker.record(kHourMs, -3600000, true, kSecondsError);
    expect(result.kind == ClockSync::Kind::Step, "a DST change is a step");
    expect(result.referenceMs == kHourMs, "measurement restarts at the step");

    result = tracker.record(2 * kHourMs, 100, false, kSecondsError);
    expect(result.kind == ClockSync::Kind::Drift, "drift resumes after the step");
    expect(result.sinceReference.spanMs == kHourMs, "span counts from the step");
    expect(result.sinceReference.driftMs == 100, "drift counts from the step");
}

void testSampleNoiseIsNotAStep()
{
    ClockSync::DriftTracker tracker;
    (void)tracker.record(0, 0, true, kSecondsError);
    // Two syncs a minute apart can differ by the sample error at both ends.
    const ClockSync::Result result = tracker.record(60000, 1200, true, kSecondsError);
    expect(result.kind == ClockSync::Kind::Drift, "1.2 s in a minute is noise");
}

void testResetAndBackwardsTime()
{
    ClockSync::DriftTracker tracker;
    (void)tracker.record(kHourMs, 0, true, kSecondsError);
    tracker.reset();
    expect(tracker.record(2 * kHourMs, 500, false, kSecondsError).kind == ClockSync::Kind::First,
           "reset forgets the measurement");
    expect(tracker.record(kHourMs, 0, false, kSecondsError).kind == ClockSync::Kind::Step,
           "time going backwards restarts");
}

void testPrecisionPerSync()
{
    ClockSync::DriftTracker tracker;
    // Started on a whole-seconds server, continued on one that sends milliseconds.
    (void)tracker.record(0, 0, true, kSecondsError);
    ClockSync::Result result = tracker.record(kHourMs, 100, false, kMillisError);
    expect(result.sinceLast.uncertaintyPpm == 208, "mixed ends: (600 + 150) ms over an hour");
    result = tracker.record(2 * kHourMs, 200, false, kMillisError);
    expect(result.sinceLast.uncertaintyPpm == 83, "both ends precise");
    expect(result.sinceReference.uncertaintyPpm == 104, "the reference keeps its own error");
    // Milliseconds tighten the step check too: 400 ms in 10 s is not noise.
    ClockSync::DriftTracker precise;
    (void)precise.record(0, 0, true, kMillisError);
    expect(precise.record(10000, 400, true, kMillisError).kind == ClockSync::Kind::Step,
           "a jump beyond both errors is a step");
}

} // namespace

int main()
{
    testArithmetic();
    testFirstSyncStartsTheMeasurement();
    testDriftAcrossKeptAndSteppedSyncs();
    testStepRestartsTheMeasurement();
    testSampleNoiseIsNotAStep();
    testResetAndBackwardsTime();
    testPrecisionPerSync();

    if (failures != 0) {
        std::cerr << failures << " ClockSync test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "ClockSync tests passed\n";
    return EXIT_SUCCESS;
}
