#pragma once

#include <cstdint>

// Turns a measured LSI error into RTC prescaler and smooth-calibration
// settings. See docs/RTC.md.
//
// The trim is how far the LSI runs from its nominal 32 kHz, in ppm: +18000
// means the LSI is at 32 576 Hz and an untrimmed clock gains 1.8%. The RTC
// divides the LSI by (PREDIV_A+1) * (PREDIV_S+1) to make 1 Hz, so the
// prescalers are chosen for the trimmed frequency. PREDIV_A is fixed at 3 so
// that PREDIV_S has steps of about 120 ppm; the remainder is taken out by the
// smooth calibration, which masks CALM of every 2^20 RTC clock cycles
// (about 0.95 ppm each).
namespace RtcTrim {

constexpr uint32_t kNominalLsiHz = 32000U;
// Covers the datasheet LSI range with margin.
constexpr int32_t kMaxTrimPpm = 100000;
constexpr uint32_t kAsynchPrediv = 3U;
constexpr uint32_t kCalibrationCycles = 1UL << 20U;

struct Settings
{
    uint32_t asynchPrediv;
    uint32_t synchPrediv;
    uint32_t minusPulses;  // CALM
};

constexpr bool isValidPpm(int32_t ppm)
{
    return ppm >= -kMaxTrimPpm && ppm <= kMaxTrimPpm;
}

// Returns false, leaving `out` untouched, for a ppm outside the valid range.
constexpr bool compute(int32_t ppm, Settings& out)
{
    if (!isValidPpm(ppm)) {
        return false;
    }
    // LSI frequency in mHz: 32 000 Hz * (1 + ppm / 1e6).
    const uint64_t lsiMilliHz =
        static_cast<uint64_t>(kNominalLsiHz / 1000U) *
        static_cast<uint64_t>(1000000 + static_cast<int64_t>(ppm));
    const uint64_t asynchDivider = kAsynchPrediv + 1U;
    // Round down, so the divided clock is never slower than 1 Hz and only
    // pulses need removing: CALP (adding pulses) is never used.
    const uint64_t synchDivider = lsiMilliHz / (asynchDivider * 1000U);
    const uint64_t dividerMilliHz = synchDivider * asynchDivider * 1000U;
    // The calibrated clock is F * 2^20 / (2^20 + CALM); CALM makes it equal
    // the divider exactly.
    const uint64_t excess = lsiMilliHz - dividerMilliHz;
    const uint64_t minusPulses =
        (excess * kCalibrationCycles + (dividerMilliHz / 2U)) / dividerMilliHz;

    out.asynchPrediv = kAsynchPrediv;
    out.synchPrediv = static_cast<uint32_t>(synchDivider - 1U);
    out.minusPulses = static_cast<uint32_t>(minusPulses);
    return true;
}

}  // namespace RtcTrim
