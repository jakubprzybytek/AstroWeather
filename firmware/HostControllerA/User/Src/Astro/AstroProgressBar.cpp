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

uint32_t progressColumns(uint8_t current, bool currentLit)
{
    uint32_t columns = columnsBelow(segmentStart(current));
    if (currentLit) {
        columns |= columnsBelow(segmentStart(static_cast<uint8_t>(current + 1U))) &
                   ~columnsBelow(segmentStart(current));
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

uint32_t fetchingColumns(HostController::FetchStage stage, uint32_t tick)
{
    // The current step blinks, so a long one (module start-up takes ~15 s on the
    // first refresh after boot) still visibly moves.
    const bool lit = ((tick / kProgressBlinkMs) % 2U) == 0U;
    return progressColumns(segmentFor(stage), lit);
}

uint32_t processingColumns()
{
    return progressColumns(kProcessingSegment, true);
}

uint8_t failedSegment(bool fetchFailed, HostController::FetchStage stage)
{
    return fetchFailed ? segmentFor(stage) : kProcessingSegment;
}

uint32_t Indicator::start(Kind kind, uint8_t failedSegment, uint32_t now)
{
    kind_ = kind;
    failedSegment_ = failedSegment;
    lit_ = true;
    until_ = now + ((kind == Kind::Success) ? kSuccessHoldMs : kFailureHoldMs);
    return (kind == Kind::Success)
               ? fullColumns()
               : progressColumns(static_cast<uint8_t>(failedSegment + 1U), false);
}

bool Indicator::expired(uint32_t now) const
{
    return static_cast<int32_t>(until_ - now) <= 0;
}

uint32_t Indicator::waitMs(uint32_t now, uint32_t maxWaitMs) const
{
    const int32_t left = static_cast<int32_t>(until_ - now);
    uint32_t wait = maxWaitMs;
    if (left > 0 && static_cast<uint32_t>(left) < wait) {
        wait = static_cast<uint32_t>(left);
    }
    if (kind_ == Kind::Failure && wait > kFailureBlinkMs) {
        wait = kFailureBlinkMs;
    }
    return wait;
}

bool Indicator::step(uint32_t& columns)
{
    if (kind_ != Kind::Failure) {
        return false;
    }
    lit_ = !lit_;
    columns = lit_ ? progressColumns(static_cast<uint8_t>(failedSegment_ + 1U), false) : 0U;
    return true;
}

} // namespace AstroProgressBar
