#include <Astro/AstroProgressBar.hpp>

#include <Expect.hpp>

#include <cstdint>

using AstroProgressBar::Indicator;
using AstroProgressBar::Row;
using HostController::FetchStage;
using Test::expect;
using Test::expectEqual;

namespace {

// Columns 0 up to, not including, `end`.
constexpr uint32_t below(uint32_t end)
{
    return (1UL << end) - 1UL;
}

void expectRow(const Row& row, uint32_t columns, uint32_t blink, const char* caseName)
{
    expectEqual(row.columns, columns, caseName);
    expectEqual(row.blink, blink, caseName);
}

// Segment boundaries across the 21 columns: 0, 3, 7, 10, 14, 17, 21.
void testSegmentLayout()
{
    const uint32_t starts[] = {0U, 3U, 7U, 10U, 14U, 17U, 21U};
    for (uint8_t segment = 0U; segment <= 6U; ++segment) {
        expectEqual(AstroProgressBar::segmentStart(segment), starts[segment], "segment start");
    }
    expectEqual(AstroProgressBar::segmentColumns(0U), below(3U), "segment 0 columns");
    expectEqual(AstroProgressBar::segmentColumns(2U), below(10U) & ~below(7U), "segment 2 columns");
    expectEqual(AstroProgressBar::segmentColumns(5U), below(21U) & ~below(17U), "segment 5 columns");
    expectEqual(AstroProgressBar::fullColumns(), 0x1FFFFFU, "full bar is 21 columns");
    expectRow(AstroProgressBar::processingRow(), 0x1FFFFFU, 0U,
              "processing lights all six segments, solid");
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

// While fetching, the finished steps are solid and the current one blinks.
void testFetchingRows()
{
    struct Case
    {
        FetchStage stage;
        uint32_t solid;
        uint32_t current;
        const char* name;
    };
    const Case cases[] = {
        {FetchStage::Queued, 0U, below(3U), "queued blinks segment 1"},
        {FetchStage::StartingModule, 0U, below(3U), "starting module blinks segment 1"},
        {FetchStage::JoiningWifi, below(3U), below(7U) & ~below(3U), "joining wifi"},
        {FetchStage::GettingIp, below(7U), below(10U) & ~below(7U), "getting ip"},
        {FetchStage::Downloading, below(10U), below(14U) & ~below(10U), "downloading"},
        {FetchStage::Disconnecting, below(14U), below(17U) & ~below(14U), "disconnecting"},
    };
    for (const Case& c : cases) {
        expectRow(AstroProgressBar::fetchingRow(c.stage), c.solid | c.current, c.current, c.name);
    }
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
    expectRow(indicator.start(Indicator::Kind::Success, 0U, start), 0x1FFFFFU, 0U,
              "success shows the full bar, solid");
    expect(indicator.active(), "success active");
    expectEqual(indicator.waitMs(start, 60000U), 1500U, "success waits for its hold");
    expectEqual(indicator.waitMs(start + 1000U, 60000U), 500U, "success waits for the rest");
    expectEqual(indicator.waitMs(start, 100U), 100U, "success wait capped by the caller");
    expect(!indicator.expired(start + 1499U), "success shown at 1499 ms");
    expect(indicator.expired(start + 1500U), "success ends at 1500 ms");
    indicator.cancel();
    expect(!indicator.active(), "cancelled");
}

// A failure shows the bar up to and including the failed step, all of it
// blinking, for a minute.
void testFailure()
{
    const uint32_t bars[] = {below(3U), below(7U), below(10U), below(14U), below(17U), below(21U)};
    for (uint8_t segment = 0U; segment < 6U; ++segment) {
        Indicator indicator{};
        expectRow(indicator.start(Indicator::Kind::Failure, segment, 0U), bars[segment],
                  bars[segment], "failure bar up to and including the failed step, blinking");
    }

    Indicator indicator{};
    const uint32_t start = 5000U;
    (void)indicator.start(Indicator::Kind::Failure, 2U, start);
    expectEqual(indicator.waitMs(start, 60000U), 60000U, "failure waits for its whole hold");
    expectEqual(indicator.waitMs(start, 1000U), 1000U, "failure wait capped by the caller");
    expectEqual(indicator.waitMs(start + 59800U, 60000U), 200U, "failure waits for the rest");
    expect(!indicator.expired(start + 59999U), "failure shown at 59999 ms");
    expect(indicator.expired(start + 60000U), "failure ends at 60 s");
    expectEqual(indicator.waitMs(start + 60000U, 60000U), 60000U, "expired: nothing to wait for");
}

void testTickWrap()
{
    Indicator indicator{};
    const uint32_t start = 0xFFFFFFFFU - 100U;
    (void)indicator.start(Indicator::Kind::Success, 0U, start);
    expect(!indicator.expired(start + 1000U), "still shown across the tick wrap");
    expectEqual(indicator.waitMs(start + 1000U, 60000U), 500U, "wait across the tick wrap");
    expect(indicator.expired(start + 1500U), "ends across the tick wrap");
}

} // namespace

int main()
{
    testSegmentLayout();
    testStageSegments();
    testFetchingRows();
    testFailedSegment();
    testSuccess();
    testFailure();
    testTickWrap();
    return Test::finish("AstroProgressBar");
}
