#include <Display/DisplayTypes.hpp>

#include <Expect.hpp>

#include <string>

// BoardAttributes: the blink and level planes, their defaults and setters,
// and the elementwise plane operations they are built on.

namespace {

using Test::expect;
using Test::expectEqual;

std::string name(const char* what, unsigned a, unsigned b)
{
    return std::string(what) + " " + std::to_string(a) + "/" + std::to_string(b);
}

void testDefaults()
{
    const Display::BoardAttributes attributes{};
    expect(attributes.blink == Display::LogicalBoardState{}, "nothing blinks by default");
    expect(attributes.level0 == Display::allElements(), "level bit 0 set everywhere");
    expect(attributes.level1 == Display::allElements(), "level bit 1 set everywhere");
    for (uint8_t display = 0; display < Display::kNumericDisplayCount; ++display) {
        for (uint8_t slot = 0; slot < Display::kSlotCount; ++slot) {
            for (uint8_t segment = 0; segment < 8U; ++segment) {
                expectEqual(attributes.numericLevel(display, slot, segment), Display::kLevelFull,
                            "numeric level is full by default");
            }
        }
    }
    for (uint8_t row = 0; row < Display::kMatrixRowCount; ++row) {
        for (uint8_t column = 0; column < Display::kMatrixColumnCount; ++column) {
            expectEqual(attributes.matrixLevel(row, column), Display::kLevelFull,
                        "matrix level is full by default");
        }
    }
    expectEqual(Display::kLevelFull, 3U, "four levels");
}

void testPlaneOperations()
{
    Display::LogicalBoardState a{};
    a.numeric[1].slots[2] = 0xF0U;
    a.matrix[3] = 0x1FFFFFU;
    Display::LogicalBoardState b{};
    b.numeric[1].slots[2] = 0x3CU;
    b.matrix[3] = 0x000FF0U;

    const Display::LogicalBoardState both = a & b;
    expectEqual(both.numeric[1].slots[2], 0x30U, "and on a numeric byte");
    expectEqual(both.matrix[3], 0x000FF0U, "and on a matrix row");
    expectEqual(both.numeric[0].slots[0], 0U, "and leaves other bytes zero");

    const Display::LogicalBoardState either = a | b;
    expectEqual(either.numeric[1].slots[2], 0xFCU, "or on a numeric byte");
    expectEqual(either.matrix[3], 0x1FFFFFU, "or on a matrix row");

    const Display::LogicalBoardState inverted = ~b;
    expectEqual(inverted.numeric[1].slots[2], 0xC3U, "not on a numeric byte");
    expectEqual(inverted.numeric[0].slots[0], 0xFFU, "not sets untouched numeric bytes");
    expectEqual(inverted.matrix[3], 0x1FF00FU, "not on a matrix row keeps bits 21-23 zero");
    expectEqual(inverted.matrix[0], Display::kMatrixMask, "not on an empty row is the 21 columns");

    expect(a == a, "equal to itself");
    expect(a != b, "different states differ");
    expect(~~a == a, "double inversion restores a masked plane");
}

void testBlinkSetters()
{
    Display::BoardAttributes attributes{};
    const Display::NumericSegments colon{{0U, 0U, 0U, 0U, 0x03U}};
    attributes.setNumericBlink(3, colon);
    expect(attributes.blink.numeric[3].slots == colon.slots, "numeric blink mask stored");
    expectEqual(attributes.blink.numeric[2].slots[4], 0U, "other displays untouched");

    attributes.setMatrixBlink(4, 0xFFFFFFFFU);
    expectEqual(attributes.blink.matrix[4], Display::kMatrixMask, "matrix blink masked to 21 columns");
    attributes.setMatrixBlink(4, 0x380U);
    expectEqual(attributes.blink.matrix[4], 0x380U, "matrix blink replaced, not accumulated");

    attributes.setNumericBlink(Display::kNumericDisplayCount, colon);
    attributes.setMatrixBlink(Display::kMatrixRowCount, 1U);
    expect(attributes.blink.numeric[3].slots == colon.slots, "out-of-range index ignored");

    attributes.clearBlink();
    expect(attributes.blink == Display::LogicalBoardState{}, "clearBlink clears everything");
    expect(attributes.level0 == Display::allElements(), "clearBlink leaves the levels");
}

void testNumericLevels()
{
    for (uint8_t level = 0; level < Display::kLevelCount; ++level) {
        Display::BoardAttributes attributes{};
        attributes.setNumericLevel(1, level);
        for (uint8_t slot = 0; slot < Display::kSlotCount; ++slot) {
            for (uint8_t segment = 0; segment < 8U; ++segment) {
                expectEqual(attributes.numericLevel(1, slot, segment), level,
                            name("whole display level", level, slot).c_str());
            }
        }
        expectEqual(attributes.numericLevel(0, 0, 0), Display::kLevelFull,
                    "other displays stay full");
        expectEqual(attributes.matrixLevel(0, 0), Display::kLevelFull, "matrix stays full");
    }

    // Only the masked segments change, in every slot the mask names.
    Display::BoardAttributes attributes{};
    Display::NumericSegments mask{};
    mask.slots[3] = 0x41U;  // A and G of the last digit
    attributes.setNumericLevel(2, mask, 1);
    expectEqual(attributes.numericLevel(2, 3, 0), 1U, "masked segment A at level 1");
    expectEqual(attributes.numericLevel(2, 3, 6), 1U, "masked segment G at level 1");
    expectEqual(attributes.numericLevel(2, 3, 1), Display::kLevelFull, "unmasked segment full");
    expectEqual(attributes.numericLevel(2, 2, 0), Display::kLevelFull, "other slot full");

    attributes.setNumericLevel(2, mask, 2);
    expectEqual(attributes.numericLevel(2, 3, 0), 2U, "level rewritten, not accumulated");
    attributes.setNumericLevel(2, mask, 0);
    expectEqual(attributes.numericLevel(2, 3, 0), 0U, "level 0 clears both bits");

    attributes.setNumericLevel(0, 7);
    expectEqual(attributes.numericLevel(0, 0, 0), Display::kLevelFull, "level above full is full");

    attributes.setNumericLevel(Display::kNumericDisplayCount, 0);
    expect(attributes.level0 != Display::LogicalBoardState{}, "out-of-range index ignored");

    attributes.clearLevels();
    expect(attributes.level0 == Display::allElements() &&
               attributes.level1 == Display::allElements(),
           "clearLevels restores full everywhere");
}

void testMatrixLevels()
{
    Display::BoardAttributes attributes{};
    attributes.setMatrixLevel(2, 0x000007U, 2);
    expectEqual(attributes.matrixLevel(2, 0), 2U, "column 0 at level 2");
    expectEqual(attributes.matrixLevel(2, 2), 2U, "column 2 at level 2");
    expectEqual(attributes.matrixLevel(2, 3), Display::kLevelFull, "column 3 stays full");
    expectEqual(attributes.matrixLevel(1, 0), Display::kLevelFull, "other row stays full");

    attributes.setMatrixLevel(2, 0xFFFFFFFFU, 1);
    for (uint8_t column = 0; column < Display::kMatrixColumnCount; ++column) {
        expectEqual(attributes.matrixLevel(2, column), 1U, "whole row at level 1");
    }
    expectEqual(attributes.level0.matrix[2], Display::kMatrixMask, "bits 21-23 never set");
    expectEqual(attributes.level1.matrix[2], 0U, "bit 1 cleared for level 1");

    attributes.setMatrixLevel(Display::kMatrixRowCount, 1U, 0);
    expectEqual(attributes.matrixLevel(0, 0), Display::kLevelFull, "out-of-range row ignored");
    expectEqual(attributes.matrixLevel(0, Display::kMatrixColumnCount), 0U,
                "out-of-range column reads as 0");
}

} // namespace

int main()
{
    testDefaults();
    testPlaneOperations();
    testBlinkSetters();
    testNumericLevels();
    testMatrixLevels();
    return Test::finish("DisplayAttributes");
}
