#pragma once

#include <HostController/St67FetchTypes.hpp>

#include <cstdint>

// The refresh progress bar on the local board's bottom matrix row: which
// columns are lit for each step and for the outcome shown afterwards. Pure
// arithmetic on ticks, so it runs in the native tests; AstroDataRefreshTask
// owns the state, draws the rows and decides when to wake. See
// docs/AstroRefresh.md#progress-bar.
//
// Six segments spread across the 21 columns, one per refresh step: module
// start-up, joining WiFi, DHCP, download, disconnect, and processing (CRC
// check, parse and publish). Column N is bit N, as for 'display matrix'.
// Ticks are milliseconds from osKernelGetTickCount().
namespace AstroProgressBar {

constexpr uint8_t kSegments = 6U;
constexpr uint8_t kProcessingSegment = 5U;  // CRC check, parse and publish
constexpr uint32_t kProgressBlinkMs = 250U;
constexpr uint32_t kSuccessHoldMs = 1500U;
constexpr uint32_t kFailureHoldMs = 60000U;
constexpr uint32_t kFailureBlinkMs = 500U;

// First column of a segment; segment kSegments gives the column count.
uint32_t segmentStart(uint8_t segment);

// Segments before `current` solid; `current` itself lit only when `currentLit`.
uint32_t progressColumns(uint8_t current, bool currentLit);

// All columns.
uint32_t fullColumns();

// The segment a WiFi task stage is shown at.
uint8_t segmentFor(HostController::FetchStage stage);

// While fetching: the current step blinks, lit in the first half of each
// 2 x kProgressBlinkMs period of the tick.
uint32_t fetchingColumns(HostController::FetchStage stage, uint32_t tick);

// After the fetch, while the response is checked, parsed and published.
uint32_t processingColumns();

// The segment a failure is shown at: a fetch failure at the step the WiFi task
// stopped at, anything after the download (CRC, parse, publish) at the last.
uint8_t failedSegment(bool fetchFailed, HostController::FetchStage stage);

// The outcome shown once a refresh has finished. Success: the full bar for
// kSuccessHoldMs. Failure: the bar up to and including the failed step,
// toggled by step() every kFailureBlinkMs, for kFailureHoldMs. Then clear.
class Indicator
{
public:
    enum class Kind : uint8_t { None, Success, Failure };

    // Starts showing an outcome; returns the columns to draw now.
    uint32_t start(Kind kind, uint8_t failedSegment, uint32_t now);
    // Stops without drawing, for a refresh that takes the row over.
    void cancel() { kind_ = Kind::None; }

    Kind kind() const { return kind_; }
    bool active() const { return kind_ != Kind::None; }
    // True once the outcome has been shown long enough; clear the row then.
    bool expired(uint32_t now) const;
    // How long the caller may wait before it must call step() or clear the
    // row: `maxWaitMs` shortened to the time left, and for a failure to
    // kFailureBlinkMs. Only while active and not expired.
    uint32_t waitMs(uint32_t now, uint32_t maxWaitMs) const;
    // A failure toggles its bar and returns true with the columns to draw;
    // anything else draws nothing and returns false.
    bool step(uint32_t& columns);

private:
    Kind kind_ = Kind::None;
    uint32_t until_ = 0U;
    uint8_t failedSegment_ = 0U;
    bool lit_ = false;
};

} // namespace AstroProgressBar
