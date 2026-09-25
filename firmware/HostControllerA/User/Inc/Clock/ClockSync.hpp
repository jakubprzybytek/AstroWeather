#pragma once

#include <cstdint>

// Setting the RTC from the time the astro API sends, and measuring the LSI
// drift between syncs. Pure arithmetic, so it runs in the native tests; the RTC
// access and the log lines are in ClockTask. See docs/RTC.md.
//
// Times are in milliseconds since 2000-01-01 00:00 local time
// (Calendar::secondsSince2000() * 1000). An offset is RTC minus server, so a
// positive offset means the RTC is ahead.
namespace ClockSync {

// One-way delay from the server stamping the time to the response headers
// arriving here. Measured from a PC on 2026-09-23: the whole HTTP round trip
// took 150..240 ms, and the stamp is taken late in handling the request.
constexpr int32_t kLatencyMs = 100;

// How far one comparison can be trusted, which depends on whether the server
// sent milliseconds.
struct Precision
{
    // Added to the server's time to estimate it when the headers arrived.
    int32_t serverOffsetMs;
    // Error of one comparison.
    int32_t sampleErrorMs;
    // The RTC is stepped only when it is at least this far out. A smaller
    // offset is within the sample error, and stepping it would add noise
    // rather than remove it.
    int32_t adjustThresholdMs;
};

// `time` with milliseconds: the error is the latency, 0..~200 ms.
constexpr Precision kMilliseconds{kLatencyMs, 150, 250};
// `time` truncated to whole seconds, as servers before 2026-09-23 sent it: the
// true time is on average half a second later than the value, and the error is
// that +-500 ms plus the latency.
constexpr Precision kWholeSeconds{500 + kLatencyMs, 600, 1000};
// A trim is suggested only once the drift is known at least this well.
constexpr int32_t kUsefulUncertaintyPpm = 50;
// Larger than any drift left on a trimmed LSI (the spread seen is ~800 ppm).
// A jump beyond it between two syncs is a step, not drift: a DST change, a
// wrong server time, or the RTC set by other means.
constexpr int32_t kMaxPlausiblePpm = 5000;

constexpr int32_t kPpmScale = 1000000;

constexpr int32_t ppmOf(int64_t driftMs, int64_t spanMs)
{
    if (spanMs <= 0) {
        return 0;
    }
    const int64_t scaled = driftMs * kPpmScale;
    return static_cast<int32_t>((scaled + (scaled >= 0 ? spanMs / 2 : -spanMs / 2)) / spanMs);
}

// The trim that cancels `driftPpm` left on `trimPpm`: (1 + trim)(1 + drift) - 1.
constexpr int32_t combinedTrimPpm(int32_t trimPpm, int32_t driftPpm)
{
    const int64_t product = static_cast<int64_t>(kPpmScale + trimPpm) * (kPpmScale + driftPpm);
    const int64_t rounded = (product + kPpmScale / 2) / kPpmScale;
    return static_cast<int32_t>(rounded - kPpmScale);
}

// LSI frequency in mHz implied by running `driftPpm` fast on `trimPpm`.
constexpr uint32_t lsiMilliHz(int32_t trimPpm, int32_t driftPpm)
{
    // 32 000 Hz = 32 000 000 mHz; 32 mHz per ppm.
    return static_cast<uint32_t>(32000000 + 32 * static_cast<int64_t>(combinedTrimPpm(trimPpm, driftPpm)));
}

// Error of a drift measured over `spanMs` between two syncs with those errors.
constexpr int32_t uncertaintyPpm(int64_t spanMs, int32_t startErrorMs, int32_t endErrorMs)
{
    return ppmOf(startErrorMs + endErrorMs, spanMs);
}

struct Span
{
    bool valid = false;
    int64_t spanMs = 0;
    int32_t driftMs = 0;  // how much the RTC gained over the span
    int32_t ppm = 0;
    int32_t uncertaintyPpm = 0;
};

enum class Kind : uint8_t
{
    First,  // no earlier sync to compare with: this one starts the measurement
    Drift,  // measured against the earlier syncs
    Step,   // an implausible jump: the measurement restarts here
};

struct Result
{
    Kind kind = Kind::First;
    Span sinceLast;       // since the previous sync
    Span sinceReference;  // since the first sync of the measurement
    int64_t referenceMs = 0;
};

// Follows the RTC's drift across syncs, including those that step it. Each sync
// measures the offset; the drift since the previous sync is that offset minus
// what was left after the previous one (zero if it stepped the RTC). The drift
// is summed from the first sync, so the rate gets more precise as the span
// grows, and the errors of the syncs in between cancel.
class DriftTracker
{
public:
    // Forget the measurement: the RTC was set or retrimmed by other means.
    void reset() { valid_ = false; }

    // Records a sync at server time `serverMs` that found `offsetMs`, good to
    // `errorMs`. When `adjusted`, the RTC was then stepped onto the server time.
    Result record(int64_t serverMs, int32_t offsetMs, bool adjusted, int32_t errorMs)
    {
        Result result{};
        if (valid_ && serverMs > lastMs_) {
            const int64_t intervalMs = serverMs - lastMs_;
            const int32_t intervalDriftMs = offsetMs - residualMs_;
            const int64_t limitMs =
                lastErrorMs_ + errorMs + intervalMs * kMaxPlausiblePpm / kPpmScale;
            const int64_t magnitude = intervalDriftMs < 0 ? -intervalDriftMs : intervalDriftMs;
            if (magnitude <= limitMs) {
                result.kind = Kind::Drift;
                result.sinceLast =
                    makeSpan(intervalMs, intervalDriftMs, lastErrorMs_ + errorMs);
                accumulatedMs_ += intervalDriftMs;
                result.sinceReference =
                    makeSpan(serverMs - referenceMs_, accumulatedMs_, referenceErrorMs_ + errorMs);
                result.referenceMs = referenceMs_;
            } else {
                result.kind = Kind::Step;
                valid_ = false;
            }
        } else if (valid_) {
            // Time went backwards or stood still between syncs.
            result.kind = Kind::Step;
            valid_ = false;
        }
        if (!valid_) {
            valid_ = true;
            referenceMs_ = serverMs;
            referenceErrorMs_ = errorMs;
            accumulatedMs_ = 0;
            result.referenceMs = serverMs;
        }
        lastMs_ = serverMs;
        lastErrorMs_ = errorMs;
        residualMs_ = adjusted ? 0 : offsetMs;
        return result;
    }

    bool valid() const { return valid_; }

private:
    // `errorMs` is the sum of the errors at the two ends.
    static Span makeSpan(int64_t spanMs, int32_t driftMs, int32_t errorMs)
    {
        Span span{};
        span.valid = true;
        span.spanMs = spanMs;
        span.driftMs = driftMs;
        span.ppm = ppmOf(driftMs, spanMs);
        span.uncertaintyPpm = ppmOf(errorMs, spanMs);
        return span;
    }

    bool valid_ = false;
    int64_t referenceMs_ = 0;
    int32_t referenceErrorMs_ = 0;
    int64_t lastMs_ = 0;
    int32_t lastErrorMs_ = 0;
    int32_t residualMs_ = 0;
    int32_t accumulatedMs_ = 0;
};

}  // namespace ClockSync
