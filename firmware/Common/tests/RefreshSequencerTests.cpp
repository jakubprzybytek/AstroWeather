#include <Display/RefreshSequencer.hpp>

#include <Expect.hpp>

#include <string>

// The order, lengths and boundaries of the passes the refresh interrupt
// steps through, and the blink phase per frame.

namespace {

using Test::expect;
using Test::expectEqual;

constexpr uint32_t kStepsPerFrame = Display::kSlotCount * Display::kPassCount;

std::string name(const char* what, unsigned a, unsigned b)
{
    return std::string(what) + " " + std::to_string(a) + "/" + std::to_string(b);
}

void testDefaultPassLengths()
{
    Display::RefreshSequencer sequencer;
    const auto& micros = sequencer.passMicros();
    uint32_t total = 0U;
    for (uint8_t pass = 0; pass < Display::kPassCount; ++pass) {
        expectEqual(micros[pass], Display::RefreshSequencer::kSlotMicros * Display::kPassPercent[pass] / 100U,
                    "pass length from the percent table");
        total += micros[pass];
    }
    expectEqual(total, Display::RefreshSequencer::kSlotMicros, "passes fill the slot");
    expectEqual(micros[0], 480U, "12 % of 4 ms");
}

void testSetPassPercentValidation()
{
    Display::RefreshSequencer sequencer;
    const auto before = sequencer.passMicros();
    expect(!sequencer.setPassPercent({12, 35, 23, 31}), "sum above 100 rejected");
    expect(!sequencer.setPassPercent({12, 35, 23, 29}), "sum below 100 rejected");
    expect(!sequencer.setPassPercent({0, 50, 25, 25}), "a zero-length pass rejected");
    expect(sequencer.passMicros() == before, "rejected table leaves the lengths");

    expect(sequencer.setPassPercent({25, 25, 25, 25}), "valid table accepted");
    expectEqual(sequencer.passMicros()[3], 1000U, "new lengths in use");
    // 33 + 33 + 33 + 1: 4000 * 33 / 100 = 1320, three of them plus 40 leaves
    // 40 us over, which go to pass 0.
    expect(sequencer.setPassPercent({33, 33, 33, 1}), "rounding table accepted");
    expectEqual(sequencer.passMicros()[0] + sequencer.passMicros()[1] +
                    sequencer.passMicros()[2] + sequencer.passMicros()[3],
                4000U, "rounding remainder keeps the slot whole");
}

// One frame: five slots of four passes, longest first within each slot,
// with the boundaries flagged.
void testFrameSequence()
{
    Display::RefreshSequencer sequencer;
    expectEqual(sequencer.frames(), 0U, "no frame before the first step");

    // Default table 12/35/23/30: longest first is pass 1, 3, 2, 0.
    const uint8_t expectedOrder[Display::kPassCount] = {1, 3, 2, 0};
    for (uint8_t slot = 0; slot < Display::kSlotCount; ++slot) {
        for (uint8_t position = 0; position < Display::kPassCount; ++position) {
            const Display::RefreshSequencer::Step step = sequencer.next();
            expectEqual(step.slot, slot, name("slot", slot, position).c_str());
            expectEqual(step.pass, expectedOrder[position], name("pass order", slot, position).c_str());
            expectEqual(step.micros, sequencer.passMicros()[step.pass],
                        name("pass length", slot, position).c_str());
            expectEqual(step.firstInSlot, position == 0U, name("firstInSlot", slot, position).c_str());
            expectEqual(step.firstInFrame, position == 0U && slot == 0U,
                        name("firstInFrame", slot, position).c_str());
            expectEqual(step.blinkPhase, Display::kBlinkOn, "first frame is the on phase");
        }
    }
    expectEqual(sequencer.frames(), 1U, "one frame started");

    const Display::RefreshSequencer::Step wrapped = sequencer.next();
    expectEqual(wrapped.slot, 0U, "wraps to slot 0");
    expect(wrapped.firstInFrame, "second frame starts");
    expectEqual(sequencer.frames(), 2U, "two frames started");
}

void testPeekDoesNotAdvance()
{
    Display::RefreshSequencer sequencer;
    const Display::RefreshSequencer::Step peeked = sequencer.peek();
    expectEqual(sequencer.frames(), 0U, "peek starts no frame");
    const Display::RefreshSequencer::Step stepped = sequencer.next();
    expectEqual(peeked.slot, stepped.slot, "peek saw the next slot");
    expectEqual(peeked.pass, stepped.pass, "peek saw the next pass");
    expectEqual(peeked.firstInFrame, stepped.firstInFrame, "peek saw the frame boundary");

    // Across the frame boundary the peeked step carries the next frame's phase.
    for (uint32_t i = 1; i < kStepsPerFrame * Display::RefreshSequencer::kBlinkHalfPeriodFrames; ++i) {
        sequencer.next();
    }
    expectEqual(sequencer.current().blinkPhase, Display::kBlinkOn, "last on-phase frame");
    expectEqual(sequencer.peek().blinkPhase, Display::kBlinkOff, "peek sees the off phase coming");
    expectEqual(sequencer.next().blinkPhase, Display::kBlinkOff, "and it arrives");
}

void testBlinkPhases()
{
    Display::RefreshSequencer sequencer;
    const uint32_t half = Display::RefreshSequencer::kBlinkHalfPeriodFrames;
    for (uint32_t frame = 1; frame <= 4U * half; ++frame) {
        const Display::RefreshSequencer::Step first = sequencer.next();
        const uint8_t expected = (((frame - 1U) / half) & 1U) != 0U ? Display::kBlinkOff
                                                                   : Display::kBlinkOn;
        expectEqual(first.blinkPhase, expected, name("phase of frame", frame, 0).c_str());
        for (uint32_t i = 1; i < kStepsPerFrame; ++i) {
            expectEqual(sequencer.next().blinkPhase, expected, name("phase within frame", frame, i).c_str());
        }
    }
    expectEqual(half, 25U, "1 Hz blink at 50 Hz");
}

} // namespace

int main()
{
    testDefaultPassLengths();
    testSetPassPercentValidation();
    testFrameSequence();
    testPeekDoesNotAdvance();
    testBlinkPhases();
    return Test::finish("RefreshSequencer");
}
