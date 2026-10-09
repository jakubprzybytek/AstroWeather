#include <Clock/RtcTrim.hpp>

#include <Expect.hpp>

#include <cmath>
#include <iostream>

namespace {

using Test::expect;

// Rate error of the resulting 1 Hz tick, in ppm, for a clock that really is
// at the trimmed frequency.
double residualPpm(int32_t ppm, const RtcTrim::Settings& settings)
{
    const double clockHz = RtcTrim::kNominalClockHz * (1.0 + ppm / 1e6);
    const double calibratedHz = clockHz * RtcTrim::kCalibrationCycles /
                                (RtcTrim::kCalibrationCycles + settings.minusPulses);
    const double tickHz =
        calibratedHz / ((settings.asynchPrediv + 1.0) * (settings.synchPrediv + 1.0));
    return (tickHz - 1.0) * 1e6;
}

void testZeroTrimIsExact()
{
    RtcTrim::Settings settings{};
    expect(RtcTrim::compute(0, settings), "zero trim computes");
    expect(settings.asynchPrediv == 124U, "zero trim asynch prescaler");
    expect(settings.synchPrediv == 5999U, "zero trim synch prescaler");
    expect(settings.minusPulses == 0U, "zero trim needs no calibration");
}

void testCrystalTrims()
{
    // A crystal 20 ppm fast: 750 015 Hz, still 125 x 6000 and 20 ppm of CALM.
    RtcTrim::Settings settings{};
    expect(RtcTrim::compute(20, settings), "small trim computes");
    expect(settings.synchPrediv == 5999U, "small trim keeps the synch prescaler");
    expect(std::fabs(residualPpm(20, settings)) < 1.0, "small trim within 1 ppm");
    expect(RtcTrim::compute(-20, settings), "negative trim computes");
    expect(settings.synchPrediv == 5998U, "slow clock takes one prescaler step down");
    expect(std::fabs(residualPpm(-20, settings)) < 1.0, "negative trim within 1 ppm");
}

void testLsiTrimIsRejected()
{
    // The first board's saved LSI trim must not reach the crystal's RTC.
    RtcTrim::Settings settings{};
    expect(!RtcTrim::isValidPpm(19300), "the old LSI trim is out of range");
    expect(!RtcTrim::compute(19300, settings), "the old LSI trim is refused");
}

void testWholeRangeWithinOnePpm()
{
    for (int32_t ppm = -RtcTrim::kMaxTrimPpm; ppm <= RtcTrim::kMaxTrimPpm; ppm += 1) {
        RtcTrim::Settings settings{};
        if (!RtcTrim::compute(ppm, settings)) {
            expect(false, "in-range trim computes");
            return;
        }
        if (settings.minusPulses > 511U || settings.synchPrediv > 0x7FFFU ||
            std::fabs(residualPpm(ppm, settings)) >= 1.0) {
            std::cerr << "ppm " << ppm << ": CALM " << settings.minusPulses << " PREDIV_S "
                      << settings.synchPrediv << " residual " << residualPpm(ppm, settings)
                      << '\n';
            expect(false, "whole range within 1 ppm and register limits");
            return;
        }
    }
}

void testOutOfRangeIsRejected()
{
    RtcTrim::Settings settings{1U, 2U, 3U};
    expect(!RtcTrim::compute(RtcTrim::kMaxTrimPpm + 1, settings), "too fast rejected");
    expect(!RtcTrim::compute(-RtcTrim::kMaxTrimPpm - 1, settings), "too slow rejected");
    expect(settings.asynchPrediv == 1U && settings.synchPrediv == 2U &&
               settings.minusPulses == 3U,
           "rejected trim leaves settings untouched");
}

} // namespace

int main()
{
    testZeroTrimIsExact();
    testCrystalTrims();
    testLsiTrimIsRejected();
    testWholeRangeWithinOnePpm();
    testOutOfRangeIsRejected();

    return Test::finish("RtcTrim");
}
