// The DisplayController's own screens (User/Src/Screens.cpp).

#include <Screens.hpp>

#include <Expect.hpp>

#include <array>
#include <cstdint>

namespace {

using Display::LogicalBoardState;
using Test::expectEqual;

using Slots = std::array<uint8_t, Display::kSlotCount>;

constexpr uint8_t kGlyph1 = 0x06U;
constexpr uint8_t kGlyph2 = 0x5BU;
constexpr uint8_t kGlyph3 = 0x4FU;
constexpr uint8_t kGlyph4 = 0x66U;
constexpr uint8_t kGlyphA = 0x77U;
constexpr uint8_t kGlyphD = 0x5EU;
constexpr uint8_t kGlyphF = 0x71U;
constexpr uint8_t kMinus = 0x40U;

void expectEveryNumeric(const LogicalBoardState& state, const Slots& expected,
                        const char* caseName)
{
    for (const Display::NumericSegments& numeric : state.numeric) {
        Test::expect(numeric.slots == expected, caseName);
    }
}

void testAllSegments()
{
    const LogicalBoardState state = DisplayController::allSegmentsState();
    expectEveryNumeric(state, {0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x07U},
                       "all segments: every digit with DP, L1-L3");
    for (uint32_t row : state.matrix) {
        expectEqual(row, Display::kMatrixMask, "all segments: every matrix dot");
    }
}

void testIdentify()
{
    const LogicalBoardState state = DisplayController::identifyState();
    const uint8_t glyphs[] = {kGlyph1, kGlyph2, kGlyph3, kGlyph4};
    for (uint8_t index = 0U; index < Display::kNumericDisplayCount; ++index) {
        const Slots expected = {glyphs[index], glyphs[index], glyphs[index], glyphs[index], 0U};
        Test::expect(state.numeric[index].slots == expected,
                     "identify: numeric n shows n+1 on all four digits");
    }
    expectEqual(state.matrix[0], 0x01U, "identify: top row lights one column");
    expectEqual(state.matrix[4], 0x1FU, "identify: bottom row lights five columns");
}

void testAddress()
{
    expectEveryNumeric(DisplayController::addressState(0x12U),
                       {kGlyphA, kGlyphD, kGlyph1, kGlyph2, 0U}, "address 0x12 shows Ad12");
    expectEveryNumeric(DisplayController::addressState(0x2AU),
                       {kGlyphA, kGlyphD, kGlyph2, kGlyphA, 0U}, "address 0x2A shows Ad2A");
    expectEveryNumeric(DisplayController::addressState(0xFFU),
                       {kGlyphA, kGlyphD, kGlyphF, kGlyphF, 0U}, "address 0xFF shows AdFF");
    expectEveryNumeric(DisplayController::addressState(0U),
                       {kGlyphA, kGlyphD, kMinus, kMinus, 0U}, "no address shows Ad--");
    expectEveryNumeric(DisplayController::addressState(0x100U),
                       {kGlyphA, kGlyphD, kMinus, kMinus, 0U}, "out of range shows Ad--");
    for (uint32_t row : DisplayController::addressState(0x12U).matrix) {
        expectEqual(row, 0U, "address: matrix blank");
    }
}

} // namespace

int main()
{
    testAllSegments();
    testIdentify();
    testAddress();
    return Test::finish("Screens");
}
