#include <Astro/AstroProgressBar.hpp>

#include <Display/DisplayTypes.hpp>

namespace AstroProgressBar {
namespace {

uint32_t columnsBelow(uint32_t end)
{
    return (end >= 32U) ? 0xFFFFFFFFU : ((1UL << end) - 1UL);
}

} // namespace

uint32_t segmentStart(uint8_t segment)
{
    return (static_cast<uint32_t>(segment) * Display::kMatrixColumnCount) / kSegments;
}

uint32_t segmentColumns(uint8_t segment)
{
    return columnsBelow(segmentStart(static_cast<uint8_t>(segment + 1U))) &
           ~columnsBelow(segmentStart(segment));
}

uint32_t progressColumns(uint8_t current, bool currentLit)
{
    uint32_t columns = columnsBelow(segmentStart(current));
    if (currentLit) {
        columns |= segmentColumns(current);
    }
    return columns;
}

uint32_t fullColumns()
{
    return columnsBelow(Display::kMatrixColumnCount);
}

uint8_t segmentFor(HostController::FetchStage stage)
{
    using HostController::FetchStage;
    switch (stage) {
    case FetchStage::Queued:
    case FetchStage::StartingModule: return 0U;
    case FetchStage::JoiningWifi: return 1U;
    case FetchStage::GettingIp: return 2U;
    case FetchStage::Downloading: return 3U;
    case FetchStage::Disconnecting: return 4U;
    }
    return 0U;
}

Row fetchingRow(HostController::FetchStage stage)
{
    // The current step blinks, so a long one (module start-up takes ~15 s on the
    // first refresh after boot) still visibly moves.
    const uint8_t segment = segmentFor(stage);
    return {progressColumns(segment, true), segmentColumns(segment)};
}

Row processingRow()
{
    return {progressColumns(kProcessingSegment, true), 0U};
}

uint8_t failedSegment(bool fetchFailed, HostController::FetchStage stage)
{
    return fetchFailed ? segmentFor(stage) : kProcessingSegment;
}

Row Indicator::start(Kind kind, uint8_t failedSegment, uint32_t now)
{
    kind_ = kind;
    until_ = now + ((kind == Kind::Success) ? kSuccessHoldMs : kFailureHoldMs);
    if (kind == Kind::Success) {
        return {fullColumns(), 0U};
    }
    const uint32_t bar = progressColumns(static_cast<uint8_t>(failedSegment + 1U), false);
    return {bar, bar};
}

bool Indicator::expired(uint32_t now) const
{
    return static_cast<int32_t>(until_ - now) <= 0;
}

uint32_t Indicator::waitMs(uint32_t now, uint32_t maxWaitMs) const
{
    const int32_t left = static_cast<int32_t>(until_ - now);
    if (left > 0 && static_cast<uint32_t>(left) < maxWaitMs) {
        return static_cast<uint32_t>(left);
    }
    return maxWaitMs;
}

} // namespace AstroProgressBar
