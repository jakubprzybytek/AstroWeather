#pragma once

#include <cstdint>

// Turns a measured RTC clock error into RTC prescaler and smooth-calibration
// settings. See Docs/RTC.md.
//
// The RTC runs from HSE / 32: the 24 MHz crystal gives 750 kHz. The trim is how
// far that clock runs from 750 kHz, in ppm: +20 means 750 015 Hz and an
// untrimmed clock gains 1.7 s a day. The RTC divides the clock by
// (PREDIV_A+1) * (PREDIV_S+1) to make 1 Hz, so the prescalers are chosen for
// the trimmed frequency. PREDIV_A is fixed at 124 so that 0 ppm gives exactly
// 125 x 6000 and PREDIV_S has steps of about 167 ppm; the remainder is taken
// out by the smooth calibration, which masks CALM of every 2^20 RTC clock
// cycles (about 0.95 ppm each).

namespace RtcTrim {

// HSE 24 MHz / 32.
constexpr uint32_t kNominalClockHz = 750000U;
// A crystal is within a few tens of ppm; anything beyond this is a trim saved
// for the LSI the RTC ran from before, which AstroWeather_Init() ignores.
constexpr int32_t kMaxTrimPpm = 1000;
constexpr uint32_t kAsynchPrediv = 124U;
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
    // Clock frequency in mHz: 750 000 Hz * (1 + ppm / 1e6).
    const uint64_t clockMilliHz =
        static_cast<uint64_t>(kNominalClockHz / 1000U) *
        static_cast<uint64_t>(1000000 + static_cast<int64_t>(ppm));
    const uint64_t asynchDivider = kAsynchPrediv + 1U;
    // Round down, so the divided clock is never slower than 1 Hz and only
    // pulses need removing: CALP (adding pulses) is never used.
    const uint64_t synchDivider = clockMilliHz / (asynchDivider * 1000U);
    const uint64_t dividerMilliHz = synchDivider * asynchDivider * 1000U;
    // The calibrated clock is F * 2^20 / (2^20 + CALM); CALM makes it equal
    // the divider exactly.
    const uint64_t excess = clockMilliHz - dividerMilliHz;
    const uint64_t minusPulses =
        (excess * kCalibrationCycles + (dividerMilliHz / 2U)) / dividerMilliHz;

    out.asynchPrediv = kAsynchPrediv;
    out.synchPrediv = static_cast<uint32_t>(synchDivider - 1U);
    out.minusPulses = static_cast<uint32_t>(minusPulses);
    return true;
}

}  // namespace RtcTrim
