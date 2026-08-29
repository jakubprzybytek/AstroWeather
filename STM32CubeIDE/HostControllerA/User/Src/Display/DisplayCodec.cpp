#include <Display/DisplayCodec.hpp>

namespace Display {
namespace {

constexpr uint8_t kSegments[] = {
    0x3FU, 0x06U, 0x5BU, 0x4FU, 0x66U,
    0x6DU, 0x7DU, 0x07U, 0x7FU, 0x6FU,
};
constexpr uint8_t kMinus = 0x40U;
constexpr uint8_t kError = 0x08U;
constexpr uint8_t kDisplaySegmentBits[kNumericDisplayCount][8] = {
    {2, 1, 4, 3, 6, 0, 7, 5},
    {5, 6, 1, 3, 2, 7, 4, 0},
    {0, 1, 3, 6, 5, 2, 7, 4},
    {0, 1, 7, 4, 5, 2, 3, 6},
};

uint8_t numericByte(NumericData value, uint8_t display, uint8_t position)
{
    const NumericMode mode = modeOf(value);
    if (mode == NumericMode::Blank) {
        return 0U;
    }
    if (mode == NumericMode::Error) {
        return static_cast<uint8_t>(1U << kDisplaySegmentBits[display][3]);
    }

    uint8_t segments = 0U;
    int32_t magnitude = value.mantissa;
    const bool negative = magnitude < 0;
    if (negative) {
        magnitude = -magnitude;
    }
    if (mode == NumericMode::Time) {
        segments = kSegments[(magnitude / 1000) % 10];
        if (position == 1U) segments = kSegments[(magnitude / 100) % 10];
        if (position == 2U) segments = kSegments[(magnitude / 10) % 10];
        if (position == 3U) segments = kSegments[magnitude % 10];
    } else if (negative && position == 0U) {
        segments = kMinus;
    } else {
        const uint8_t digitCount = negative ? 3U : 4U;
        const uint8_t digit = static_cast<uint8_t>(digitCount - 1U - position);
        if (position >= (negative ? 1U : 0U)) {
            const uint32_t divisor = digit == 0U ? 1U :
                digit == 1U ? 10U : digit == 2U ? 100U : 1000U;
            const uint8_t number = static_cast<uint8_t>((magnitude / divisor) % 10);
            const bool leading = number == 0U && magnitude < static_cast<int32_t>(divisor * 10U);
            segments = leading ? 0U : kSegments[number];
        }
    }
    const uint8_t precision = precisionOf(value);
    if (precision != 0U && position == static_cast<uint8_t>(3U - precision)) {
        segments |= 0x80U;
    }
    uint8_t encoded = 0U;
    for (uint8_t segment = 0; segment < 8U; ++segment) {
        if ((segments & (1U << segment)) != 0U) {
            encoded |= static_cast<uint8_t>(1U << kDisplaySegmentBits[display][segment]);
        }
    }
    return encoded;
}

uint8_t specialByte(NumericData value, uint8_t display)
{
    if (modeOf(value) != NumericMode::Time || !doubleDotsOf(value)) {
        return 0U;
    }
    return static_cast<uint8_t>((1U << kDisplaySegmentBits[display][0]) |
                                (1U << kDisplaySegmentBits[display][1]));
}

} // namespace

void encodePcb(const LogicalBoardState& state, PreparedFrame& frame)
{
    frame.fill(0U);
    for (uint8_t slot = 0; slot < kSlotCount; ++slot) {
        uint8_t* output = &frame[slot * kBytesPerSlot];
        output[0] = slot == 4U ? specialByte(state.numeric[3], 3U)
                       : numericByte(state.numeric[3], 3U, slot);
        output[1] = slot == 4U ? specialByte(state.numeric[2], 2U)
                       : numericByte(state.numeric[2], 2U, slot);
        if (slot < kMatrixRowCount) {
            const uint32_t row = state.matrix[slot] & kMatrixMask;
            output[2] = static_cast<uint8_t>(row >> 16U);
            output[3] = static_cast<uint8_t>(row >> 8U);
            output[4] = static_cast<uint8_t>(row);
        }
        output[5] = slot == 4U ? specialByte(state.numeric[1], 1U)
                               : numericByte(state.numeric[1], 1U, slot);
        output[6] = slot == 4U ? specialByte(state.numeric[0], 0U)
                               : numericByte(state.numeric[0], 0U, slot);
    }
}

} // namespace Display
