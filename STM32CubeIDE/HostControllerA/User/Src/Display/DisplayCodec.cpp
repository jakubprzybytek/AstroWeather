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
            const uint32_t row = state.matrix[slot] & kMatrixMask;
            output[2] = static_cast<uint8_t>(row >> 16U);
            output[3] = static_cast<uint8_t>(row >> 8U);
            output[4] = static_cast<uint8_t>(row);
        }
        output[5] = mapSegments(state.numeric[1].slots[slot], 1U);
        output[6] = mapSegments(state.numeric[0].slots[slot], 0U);
    }
}

} // namespace Display
