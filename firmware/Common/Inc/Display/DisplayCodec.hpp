#pragma once

#include <Display/DisplayTypes.hpp>

#include <array>

namespace Display {

void encodePcb(const LogicalBoardState& state, PreparedFrame& frame);

// Brightness levels and blinking are made in time: each multiplexing slot
// is shown as kPassCount passes of different lengths, and a lit element at
// level L is on only during the passes in kLevelPasses[L]. Its light is
// then the sum of those passes' shares of the slot. Levels 1, 2 and 3 of a
// numeric display get 12 %, 39 % and 100 % of the slot, percentages chosen
// by eye for even-looking steps. The matrix LEDs are brighter than the
// numeric ones, so the matrix sits out the last pass and its levels get
// 12 %, 39 % and 70 %.
constexpr uint8_t kPassCount = 4;
constexpr uint8_t kPassPercent[kPassCount] = {12, 39, 19, 30};
constexpr uint8_t kLevelPasses[kLevelCount] = {0x0U, 0x1U, 0x2U, 0xFU};
constexpr uint8_t kMatrixPasses = 0x7U;

// A blinking element is shown in the on phase only. The refresh alternates
// the phases; the frames for both are prepared in advance.
constexpr uint8_t kBlinkPhaseCount = 2;
constexpr uint8_t kBlinkOn = 0;
constexpr uint8_t kBlinkOff = 1;

// Prepared frames for every blink phase and pass: [phase][pass].
using PassFrames = std::array<std::array<PreparedFrame, kPassCount>, kBlinkPhaseCount>;

// The elements of `lit` that are on during one pass of one blink phase.
LogicalBoardState passElements(const LogicalBoardState& lit, const BoardAttributes& attributes,
                               uint8_t pass, uint8_t phase);

// Encodes the content with its attributes into the frame of every phase and
// pass, using the same PCB mapping as encodePcb().
void encodePasses(const LogicalBoardState& lit, const BoardAttributes& attributes,
                  PassFrames& frames);

// The share of a slot an element at `level` is lit for, in percent, for a
// numeric segment or a matrix pixel.
constexpr uint8_t numericLevelPercent(uint8_t level)
{
    uint8_t percent = 0U;
    for (uint8_t pass = 0; pass < kPassCount; ++pass) {
        if (((kLevelPasses[level] >> pass) & 1U) != 0U) {
            percent = static_cast<uint8_t>(percent + kPassPercent[pass]);
        }
    }
    return percent;
}

constexpr uint8_t matrixLevelPercent(uint8_t level)
{
    uint8_t percent = 0U;
    for (uint8_t pass = 0; pass < kPassCount; ++pass) {
        if (((kLevelPasses[level] & kMatrixPasses) >> pass & 1U) != 0U) {
            percent = static_cast<uint8_t>(percent + kPassPercent[pass]);
        }
    }
    return percent;
}

static_assert(numericLevelPercent(kLevelFull) == 100U, "the passes make up the whole slot");
static_assert(numericLevelPercent(0) == 0U, "level 0 is off");

} // namespace Display
