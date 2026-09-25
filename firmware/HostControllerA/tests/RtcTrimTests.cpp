#include <Clock/RtcTrim.hpp>

#include <Expect.hpp>

#include <cmath>
#include <iostream>

namespace {

using Test::expect;

// Rate error of the resulting 1 Hz tick, in ppm, for an LSI that really is
// at the trimmed frequency.
double residualPpm(int32_t ppm, const RtcTrim::Settings& settings)
{
    const double lsiHz = RtcTrim::kNominalLsiHz * (1.0 + ppm / 1e6);
    const double calibratedHz = lsiHz * RtcTrim::kCalibrationCycles /
                                (RtcTrim::kCalibrationCycles + settings.minusPulses);
    const double tickHz =
        calibratedHz / ((settings.asynchPrediv + 1.0) * (settings.synchPrediv + 1.0));
    return (tickHz - 1.0) * 1e6;
}

void testZeroTrimIsExact()
{
    RtcTrim::Settings settings{};
    expect(RtcTrim::compute(0, settings), "zero trim computes");
    expect(settings.asynchPrediv == 3U, "zero trim asynch prescaler");
    expect(settings.synchPrediv == 7999U, "zero trim synch prescaler");
    expect(settings.minusPulses == 0U, "zero trim needs no calibration");
}

void testMeasuredBoardValue()
{
    // 18372 ppm, as measured on the first board: LSI about 32 588 Hz.
    RtcTrim::Settings settings{};
    expect(RtcTrim::compute(18372, settings), "measured trim computes");
    expect(settings.synchPrediv == 8145U, "measured trim synch prescaler");
    expect(std::fabs(residualPpm(18372, settings)) < 1.0, "measured trim within 1 ppm");
}

void testWholeRangeWithinOnePpm()
{
    for (int32_t ppm = -RtcTrim::kMaxTrimPpm; ppm <= RtcTrim::kMaxTrimPpm; ppm += 7) {
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
    testMeasuredBoardValue();
    testWholeRangeWithinOnePpm();
    testOutOfRangeIsRejected();

    return Test::finish("RtcTrim");
}
