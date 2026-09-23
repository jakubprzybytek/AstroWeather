#include <HostController/AstroProgressBar.hpp>

#include <Expect.hpp>

#include <cstdint>

using AstroProgressBar::Indicator;
using HostController::FetchStage;
using Test::expect;
using Test::expectEqual;

namespace {

// Columns 0 up to, not including, `end`.
constexpr uint32_t below(uint32_t end)
{
    return (1UL << end) - 1UL;
}

// Segment boundaries across the 21 columns: 0, 3, 7, 10, 14, 17, 21.
void testSegmentLayout()
{
    const uint32_t starts[] = {0U, 3U, 7U, 10U, 14U, 17U, 21U};
    for (uint8_t segment = 0U; segment <= 6U; ++segment) {
        expectEqual(AstroProgressBar::segmentStart(segment), starts[segment], "segment start");
    }
    expectEqual(AstroProgressBar::fullColumns(), 0x1FFFFFU, "full bar is 21 columns");
    expectEqual(AstroProgressBar::processingColumns(), 0x1FFFFFU,
                "processing lights all six segments");
    expectEqual(AstroProgressBar::progressColumns(0U, false), 0U, "nothing before segment 0");
    expectEqual(AstroProgressBar::progressColumns(2U, false), below(7U), "two segments solid");
    expectEqual(AstroProgressBar::progressColumns(2U, true), below(10U),
                "two solid and the third lit");
}

void testStageSegments()
{
    expectEqual(AstroProgressBar::segmentFor(FetchStage::Queued), 0U, "queued");
    expectEqual(AstroProgressBar::segmentFor(FetchStage::StartingModule), 0U, "starting module");
    expectEqual(AstroProgressBar::segmentFor(FetchStage::JoiningWifi), 1U, "joining wifi");
    expectEqual(AstroProgressBar::segmentFor(FetchStage::GettingIp), 2U, "getting ip");
    expectEqual(AstroProgressBar::segmentFor(FetchStage::Downloading), 3U, "downloading");
    expectEqual(AstroProgressBar::segmentFor(FetchStage::Disconnecting), 4U, "disconnecting");
}

void testFetchingColumns()
{
    struct Case
    {
        FetchStage stage;
        uint32_t lit;
        uint32_t unlit;
        const char* name;
    };
    const Case cases[] = {
        {FetchStage::Queued, below(3U), 0U, "queued blinks segment 1"},
        {FetchStage::StartingModule, below(3U), 0U, "starting module blinks segment 1"},
        {FetchStage::JoiningWifi, below(7U), below(3U), "joining wifi"},
        {FetchStage::GettingIp, below(10U), below(7U), "getting ip"},
        {FetchStage::Downloading, below(14U), below(10U), "downloading"},
        {FetchStage::Disconnecting, below(17U), below(14U), "disconnecting"},
    };
    for (const Case& c : cases) {
        expectEqual(AstroProgressBar::fetchingColumns(c.stage, 0U), c.lit, c.name);
        expectEqual(AstroProgressBar::fetchingColumns(c.stage, 250U), c.unlit, c.name);
    }
}

void testBlinkPhases()
{
    const FetchStage stage = FetchStage::GettingIp;
    const uint32_t lit = below(10U);
    const uint32_t unlit = below(7U);
    expectEqual(AstroProgressBar::fetchingColumns(stage, 0U), lit, "blink tick 0 lit");
    expectEqual(AstroProgressBar::fetchingColumns(stage, 249U), lit, "blink tick 249 lit");
    expectEqual(AstroProgressBar::fetchingColumns(stage, 250U), unlit, "blink tick 250 off");
    expectEqual(AstroProgressBar::fetchingColumns(stage, 499U), unlit, "blink tick 499 off");
    expectEqual(AstroProgressBar::fetchingColumns(stage, 500U), lit, "blink tick 500 lit");
    expectEqual(AstroProgressBar::fetchingColumns(stage, 750U), unlit, "blink tick 750 off");
    // The phase follows the tick, so it is not continuous across the 32-bit wrap:
    // 0xFFFFFFFF / 250 is odd, then tick 0 is lit again.
    expectEqual(AstroProgressBar::fetchingColumns(stage, 0xFFFFFFFFU), unlit,
                "blink at the last tick before the wrap");
}

void testFailedSegment()
{
    expectEqual(AstroProgressBar::failedSegment(true, FetchStage::StartingModule), 0U,
                "fetch failed starting the module");
    expectEqual(AstroProgressBar::failedSegment(true, FetchStage::JoiningWifi), 1U,
                "fetch failed joining");
    expectEqual(AstroProgressBar::failedSegment(true, FetchStage::Disconnecting), 4U,
                "fetch failed disconnecting");
    expectEqual(AstroProgressBar::failedSegment(false, FetchStage::Disconnecting), 5U,
                "crc, parse or publish failure at the processing segment");
    expectEqual(AstroProgressBar::failedSegment(false, FetchStage::Queued), 5U,
                "processing failure whatever the stage");
}

void testSuccess()
{
    Indicator indicator{};
    expect(!indicator.active(), "indicator starts idle");
    const uint32_t start = 1000U;
    expectEqual(indicator.start(Indicator::Kind::Success, 0U, start), 0x1FFFFFU,
                "success shows the full bar");
    expect(indicator.active(), "success active");
    expectEqual(indicator.waitMs(start, 60000U), 1500U, "success waits for its hold");
    expectEqual(indicator.waitMs(start + 1000U, 60000U), 500U, "success waits for the rest");
    expectEqual(indicator.waitMs(start, 100U), 100U, "success wait capped by the caller");
    uint32_t columns = 0xABCDU;
    expect(!indicator.step(columns), "success does not blink");
    expectEqual(columns, 0xABCDU, "success step draws nothing");
    expect(!indicator.expired(start + 1499U), "success shown at 1499 ms");
    expect(indicator.expired(start + 1500U), "success ends at 1500 ms");
    indicator.cancel();
    expect(!indicator.active(), "cancelled");
}

void testFailurePatterns()
{
    const uint32_t bars[] = {below(3U), below(7U), below(10U), below(14U), below(17U), below(21U)};
    for (uint8_t segment = 0U; segment < 6U; ++segment) {
        Indicator indicator{};
        expectEqual(indicator.start(Indicator::Kind::Failure, segment, 0U), bars[segment],
                    "failure bar up to and including the failed step");
        uint32_t columns = 0xABCDU;
        expect(indicator.step(columns), "failure blinks");
        expectEqual(columns, 0U, "failure blink off");
        expect(indicator.step(columns), "failure blinks again");
        expectEqual(columns, bars[segment], "failure blink on");
    }
}

void testFailureTiming()
{
    Indicator indicator{};
    const uint32_t start = 5000U;
    (void)indicator.start(Indicator::Kind::Failure, 2U, start);
    expectEqual(indicator.waitMs(start, 60000U), 500U, "failure wakes to blink");
    expectEqual(indicator.waitMs(start + 59800U, 60000U), 200U, "failure waits for the rest");
    expect(!indicator.expired(start + 59999U), "failure shown at 59999 ms");
    expect(indicator.expired(start + 60000U), "failure ends at 60 s");

    // As AstroDataRefreshTask::run() drives it with prompt wake-ups: wait,
    // step, until expired. Toggles every 500 ms, 120 times in 60 s.
    uint32_t now = start;
    uint32_t steps = 0U;
    uint32_t litSteps = 0U;
    while (!indicator.expired(now) && steps < 1000U) {
        now += indicator.waitMs(now, 60000U);
        uint32_t columns = 0U;
        if (indicator.step(columns)) {
            ++steps;
            litSteps += (columns != 0U) ? 1U : 0U;
        }
    }
    expectEqual(now, start + 60000U, "failure run ends at 60 s");
    expectEqual(steps, 120U, "failure toggles 120 times");
    expectEqual(litSteps, 60U, "failure lit half the toggles");
}

void testTickWrap()
{
    Indicator indicator{};
    const uint32_t start = 0xFFFFFF00U;
    (void)indicator.start(Indicator::Kind::Success, 0U, start);
    expect(!indicator.expired(start + 1499U), "success across the tick wrap");
    expect(indicator.expired(start + 1500U), "success ends across the tick wrap");
    expectEqual(indicator.waitMs(start + 1000U, 60000U), 500U, "wait across the tick wrap");
}

} // namespace

int main()
{
    testSegmentLayout();
    testStageSegments();
    testFetchingColumns();
    testBlinkPhases();
    testFailedSegment();
    testSuccess();
    testFailurePatterns();
    testFailureTiming();
    testTickWrap();
    return Test::finish("AstroProgressBar");
}
