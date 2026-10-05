#include <Display/BootScreens.hpp>

#include "cmsis_os2.h"

namespace Display {
namespace {

// Normalized segment bits: A = bit 0 ... G = bit 6, DP = bit 7 (DisplayTypes).
constexpr uint8_t kHexGlyphs[16] = {
    0x3FU, 0x06U, 0x5BU, 0x4FU, 0x66U, 0x6DU, 0x7DU, 0x07U,
    0x7FU, 0x6FU, 0x77U, 0x7CU, 0x39U, 0x5EU, 0x79U, 0x71U,
};
constexpr uint8_t kGlyphA = 0x77U;
constexpr uint8_t kGlyphD = 0x5EU; // lower-case d
constexpr uint8_t kGlyphMinus = 0x40U;
constexpr uint8_t kAllDigitSegments = 0xFFU;
// Slot 4 carries the indicators L1, L2 and L3 as segments A, B and C.
constexpr uint8_t kIndicatorSlot = 4U;
constexpr uint8_t kAllIndicators = 0x07U;

} // namespace

LogicalBoardState slotTestState(uint8_t slot)
{
    LogicalBoardState state{};
    if (slot >= kSlotCount) {
        return state;
    }
    const uint8_t segments = (slot == kIndicatorSlot) ? kAllIndicators : kAllDigitSegments;
    for (NumericSegments& numeric : state.numeric) {
        numeric.slots[slot] = segments;
    }
    state.matrix[kMatrixRowCount - 1U - slot] = kMatrixMask;
    return state;
}

LogicalBoardState addressState(uint16_t address)
{
    const bool valid = address >= 0x01U && address <= 0xFFU;
    const uint8_t high = valid ? kHexGlyphs[(address >> 4U) & 0x0FU] : kGlyphMinus;
    const uint8_t low = valid ? kHexGlyphs[address & 0x0FU] : kGlyphMinus;

    LogicalBoardState state{};
    state.numeric[0].slots = {kGlyphA, kGlyphD, high, low, 0U};
    return state;
}

void showBootScreens(DisplayBoard& board, uint16_t address)
{
    const BoardAttributes plain{};
    for (uint8_t slot = 0U; slot < kSlotCount; ++slot) {
        board.show(slotTestState(slot), plain);
        osDelay(kSlotTestMs);
    }
    board.show(addressState(address), plain);
    osDelay(kBootAddressMs);
}

} // namespace Display
