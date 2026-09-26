#include <Display/DisplayCodec.hpp>

namespace Display {
namespace {

constexpr uint8_t kDisplaySegmentBits[kNumericDisplayCount][8] = {
    {2, 1, 4, 3, 6, 0, 7, 5},
    {5, 6, 1, 3, 2, 7, 4, 0},
    {0, 1, 3, 6, 5, 2, 7, 4},
    {0, 1, 7, 4, 5, 2, 3, 6},
};

uint8_t mapSegments(uint8_t normalized, uint8_t display)
{
    uint8_t encoded = 0U;
    for (uint8_t segment = 0; segment < 8U; ++segment) {
        if ((normalized & (1U << segment)) != 0U) {
            encoded |= static_cast<uint8_t>(1U << kDisplaySegmentBits[display][segment]);
        }
    }
    return encoded;
}

} // namespace

void encodePcb(const LogicalBoardState& state, PreparedFrame& frame)
{
    frame.fill(0U);
    for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
        uint8_t* output = &frame[slot * kBytesPerSlot];
        output[0] = mapSegments(state.numeric[3].slots[slot], 3U);
        output[1] = mapSegments(state.numeric[2].slots[slot], 2U);
        if (slot < kMatrixRowCount) {
            const uint8_t logicalRow = static_cast<uint8_t>(kMatrixRowCount - 1U - slot);
            const uint32_t row = state.matrix[logicalRow] & kMatrixMask;
            output[2] = static_cast<uint8_t>(row >> 16U);
            output[3] = static_cast<uint8_t>(row >> 8U);
            output[4] = static_cast<uint8_t>(row);
        }
        output[5] = mapSegments(state.numeric[1].slots[slot], 1U);
        output[6] = mapSegments(state.numeric[0].slots[slot], 0U);
    }
}

namespace {

// The elements whose level is exactly `level`.
LogicalBoardState atLevel(const BoardAttributes& attributes, uint8_t level)
{
    const LogicalBoardState bit0 = (level & 1U) != 0U ? attributes.level0 : ~attributes.level0;
    const LogicalBoardState bit1 = (level & 2U) != 0U ? attributes.level1 : ~attributes.level1;
    return bit0 & bit1;
}

} // namespace

LogicalBoardState passElements(const LogicalBoardState& lit, const BoardAttributes& attributes,
                               uint8_t pass, uint8_t phase)
{
    const LogicalBoardState visible = phase == kBlinkOff ? (lit & ~attributes.blink) : lit;
    LogicalBoardState elements{};
    for (uint8_t level = 0; level < kLevelCount; ++level) {
        if (((kLevelPasses[level] >> pass) & 1U) != 0U) {
            elements = elements | (visible & atLevel(attributes, level));
        }
    }
    if (((kMatrixPasses >> pass) & 1U) == 0U) {
        elements.matrix.fill(0U);
    }
    return elements;
}

void encodePasses(const LogicalBoardState& lit, const BoardAttributes& attributes,
                  PassFrames& frames)
{
    for (uint8_t phase = 0; phase < kBlinkPhaseCount; ++phase) {
        for (uint8_t pass = 0; pass < kPassCount; ++pass) {
            encodePcb(passElements(lit, attributes, pass, phase), frames[phase][pass]);
        }
    }
}

} // namespace Display
