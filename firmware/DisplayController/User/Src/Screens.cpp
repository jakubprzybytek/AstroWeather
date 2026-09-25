#include <Screens.hpp>

namespace DisplayController {
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
constexpr uint8_t kAllIndicators = 0x07U;

} // namespace

Display::LogicalBoardState allSegmentsState()
{
    Display::LogicalBoardState state{};
    for (Display::NumericSegments& numeric : state.numeric) {
        numeric.slots = {kAllDigitSegments, kAllDigitSegments, kAllDigitSegments,
                         kAllDigitSegments, kAllIndicators};
    }
    state.matrix.fill(Display::kMatrixMask);
    return state;
}

Display::LogicalBoardState identifyState()
{
    Display::LogicalBoardState state{};
    for (uint8_t index = 0U; index < Display::kNumericDisplayCount; ++index) {
        const uint8_t glyph = kHexGlyphs[index + 1U];
        state.numeric[index].slots = {glyph, glyph, glyph, glyph, 0U};
    }
    for (uint8_t row = 0U; row < Display::kMatrixRowCount; ++row) {
        state.matrix[row] = (1UL << (row + 1U)) - 1UL;
    }
    return state;
}

Display::LogicalBoardState addressState(uint16_t address)
{
    const bool valid = address >= 0x01U && address <= 0xFFU;
    const uint8_t high = valid ? kHexGlyphs[(address >> 4U) & 0x0FU] : kGlyphMinus;
    const uint8_t low = valid ? kHexGlyphs[address & 0x0FU] : kGlyphMinus;

    Display::LogicalBoardState state{};
    for (Display::NumericSegments& numeric : state.numeric) {
        numeric.slots = {kGlyphA, kGlyphD, high, low, 0U};
    }
    return state;
}

} // namespace DisplayController
