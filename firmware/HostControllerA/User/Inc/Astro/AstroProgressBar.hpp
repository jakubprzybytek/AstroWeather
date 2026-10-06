#pragma once

#include <WiFi/St67FetchTypes.hpp>

#include <cstdint>

// The refresh progress bar on the local board's bottom matrix row: which
// columns are lit and which blink for each step, and for the outcome shown
// afterwards. Pure arithmetic, so it runs in the native tests;
// AstroDataRefreshTask owns the state, draws the rows and decides when to
// wake. See Docs/AstroRefresh.md#progress-bar.
//
// Six segments spread across the 21 columns, one per refresh step: module
// start-up, joining WiFi, DHCP, download, disconnect, and processing (CRC
// check, parse and publish). Column N is bit N, as for 'display row'. The
// blinking itself is the display's: a column in `blink` is lit in the
// display's blink-on phase only, so the bar needs no periodic redraw.
// Ticks are milliseconds from osKernelGetTickCount().
namespace AstroProgressBar {

constexpr uint8_t kSegments = 6U;
constexpr uint8_t kProcessingSegment = 5U;  // CRC check, parse and publish
constexpr uint32_t kSuccessHoldMs = 1500U;
constexpr uint32_t kFailureHoldMs = 60000U;

// What the row shows: the lit columns, and which of them blink.
struct Row
{
    uint32_t columns;
    uint32_t blink;

    bool operator==(const Row& other) const
    {
        return columns == other.columns && blink == other.blink;
    }
    bool operator!=(const Row& other) const { return !(*this == other); }
};

// First column of a segment; segment kSegments gives the column count.
uint32_t segmentStart(uint8_t segment);

// The columns of one segment.
uint32_t segmentColumns(uint8_t segment);

// Segments before `current` solid; `current` itself lit only when `currentLit`.
uint32_t progressColumns(uint8_t current, bool currentLit);

// All columns.
uint32_t fullColumns();

// The segment a WiFi task stage is shown at.
uint8_t segmentFor(HostController::FetchStage stage);

// While fetching: finished steps solid, the current one blinking.
Row fetchingRow(HostController::FetchStage stage);

// After the fetch, while the response is checked, parsed and published:
// every segment solid.
Row processingRow();

// The segment a failure is shown at: a fetch failure at the step the WiFi task
// stopped at, anything after the download (CRC, parse, publish) at the last.
uint8_t failedSegment(bool fetchFailed, HostController::FetchStage stage);

// The outcome shown once a refresh has finished. Success: the full bar, solid,
// for kSuccessHoldMs. Failure: the bar up to and including the failed step,
// blinking, for kFailureHoldMs. Then clear.
class Indicator
{
public:
    enum class Kind : uint8_t { None, Success, Failure };

    // Starts showing an outcome; returns the row to draw.
    Row start(Kind kind, uint8_t failedSegment, uint32_t now);
    // Stops without drawing, for a refresh that takes the row over.
    void cancel() { kind_ = Kind::None; }

    Kind kind() const { return kind_; }
    bool active() const { return kind_ != Kind::None; }
    // True once the outcome has been shown long enough; clear the row then.
    bool expired(uint32_t now) const;
    // How long the caller may wait before it must clear the row: `maxWaitMs`
    // shortened to the time left. Only while active and not expired.
    uint32_t waitMs(uint32_t now, uint32_t maxWaitMs) const;

private:
    Kind kind_ = Kind::None;
    uint32_t until_ = 0U;
};

} // namespace AstroProgressBar
