#include <Display/DisplayCodec.hpp>

#include <Expect.hpp>

#include <string>

// Levels and blinking as passes: the pass table's invariants, which elements
// each pass shows, and that encodePasses() is encodePcb() of exactly those.

namespace {

using Test::expect;
using Test::expectEqual;

std::string name(const char* what, unsigned a, unsigned b)
{
    return std::string(what) + " " + std::to_string(a) + "/" + std::to_string(b);
}

void testPassTable()
{
    unsigned total = 0U;
    for (uint8_t pass = 0; pass < Display::kPassCount; ++pass) {
        total += Display::kPassPercent[pass];
        expect(Display::kPassPercent[pass] > 0U, "every pass has a length");
    }
    expectEqual(total, 100U, "passes make up the whole slot");

    expectEqual(Display::numericLevelPercent(0), 0U, "numeric level 0 off");
    expectEqual(Display::numericLevelPercent(1), 12U, "numeric level 1 is 12 %");
    expectEqual(Display::numericLevelPercent(2), 35U, "numeric level 2 is 35 %");
    expectEqual(Display::numericLevelPercent(3), 100U, "numeric level 3 is full");
    expectEqual(Display::matrixLevelPercent(0), 0U, "matrix level 0 off");
    expectEqual(Display::matrixLevelPercent(1), 12U, "matrix level 1 is 12 %");
    expectEqual(Display::matrixLevelPercent(2), 35U, "matrix level 2 is 35 %");
    expectEqual(Display::matrixLevelPercent(3), 70U, "matrix full is 70 %");

    for (uint8_t level = 1; level < Display::kLevelCount; ++level) {
        expect(Display::numericLevelPercent(level) > Display::numericLevelPercent(level - 1U),
               "numeric levels increase");
        expect(Display::matrixLevelPercent(level) > Display::matrixLevelPercent(level - 1U),
               "matrix levels increase");
    }
}

// One lit element of each kind at a chosen level; which passes show it.
void testPassElementsFollowTheTable()
{
    for (uint8_t level = 0; level < Display::kLevelCount; ++level) {
        Display::LogicalBoardState lit{};
        lit.numeric[0].slots[1] = 0x02U;  // segment B
        lit.matrix[2] = 1UL << 20U;       // column 21
        Display::BoardAttributes attributes{};
        attributes.setNumericLevel(0, level);
        attributes.setMatrixLevel(2, 1UL << 20U, level);

        for (uint8_t pass = 0; pass < Display::kPassCount; ++pass) {
            const Display::LogicalBoardState shown =
                Display::passElements(lit, attributes, pass, Display::kBlinkOn);
            const bool numericOn = ((Display::kLevelPasses[level] >> pass) & 1U) != 0U;
            const bool matrixOn =
                (((Display::kLevelPasses[level] & Display::kMatrixPasses) >> pass) & 1U) != 0U;
            expectEqual(shown.numeric[0].slots[1], numericOn ? 0x02U : 0U,
                        name("numeric segment in pass", level, pass).c_str());
            expectEqual(shown.matrix[2], matrixOn ? (1UL << 20U) : 0U,
                        name("matrix pixel in pass", level, pass).c_str());
        }
    }
}

void testUnlitElementsStayOff()
{
    Display::LogicalBoardState lit{};
    lit.numeric[3].slots[0] = 0x01U;
    Display::BoardAttributes attributes{};  // full everywhere, including unlit elements
    for (uint8_t pass = 0; pass < Display::kPassCount; ++pass) {
        const Display::LogicalBoardState shown =
            Display::passElements(lit, attributes, pass, Display::kBlinkOn);
        expect(shown == lit, "only the lit element shows, whatever the level planes say");
    }
}

void testBlinkPhases()
{
    Display::LogicalBoardState lit{};
    lit.numeric[3].slots[4] = 0x03U;  // colon
    lit.numeric[3].slots[0] = 0x5BU;  // a digit that does not blink
    lit.matrix[4] = 0x3FFU;
    Display::BoardAttributes attributes{};
    attributes.setNumericBlink(3, Display::NumericSegments{{0U, 0U, 0U, 0U, 0x03U}});
    attributes.setMatrixBlink(4, 0x380U);

    for (uint8_t pass = 0; pass < Display::kPassCount; ++pass) {
        const Display::LogicalBoardState on =
            Display::passElements(lit, attributes, pass, Display::kBlinkOn);
        const Display::LogicalBoardState off =
            Display::passElements(lit, attributes, pass, Display::kBlinkOff);
        // Everything is at full level, so every pass shows the numeric
        // elements and the matrix passes show the matrix ones.
        expectEqual(on.numeric[3].slots[4], 0x03U, "colon on in the on phase");
        expectEqual(off.numeric[3].slots[4], 0U, "colon off in the off phase");
        expectEqual(off.numeric[3].slots[0], 0x5BU, "digit stays in the off phase");
        if (((Display::kMatrixPasses >> pass) & 1U) != 0U) {
            expectEqual(on.matrix[4], 0x3FFU, "whole bar in the on phase");
            expectEqual(off.matrix[4], 0x07FU, "blinking columns off in the off phase");
        }
    }
}

void testEncodePassesIsEncodePcbOfEachPass()
{
    Display::LogicalBoardState lit{};
    lit.numeric[0].slots[0] = 0xFFU;
    lit.numeric[1].slots[2] = 0x7FU;
    lit.numeric[2].slots[4] = 0x07U;
    lit.matrix = {0x1FFFFFU, 0x155555U, 0x0AAAAAU, 0x000001U, 0x100000U};
    Display::BoardAttributes attributes{};
    attributes.setNumericLevel(0, 1);
    attributes.setNumericLevel(1, 2);
    attributes.setMatrixLevel(1, 0x0FFFFFU, 1);
    attributes.setMatrixLevel(2, 0xFFFFFFU, 2);
    attributes.setNumericBlink(2, Display::NumericSegments{{0U, 0U, 0U, 0U, 0x01U}});
    attributes.setMatrixBlink(0, 0x1U);

    Display::PassFrames frames{};
    Display::encodePasses(lit, attributes, frames);

    for (uint8_t phase = 0; phase < Display::kBlinkPhaseCount; ++phase) {
        for (uint8_t pass = 0; pass < Display::kPassCount; ++pass) {
            Display::PreparedFrame expected{};
            Display::encodePcb(Display::passElements(lit, attributes, pass, phase), expected);
            expect(frames[phase][pass] == expected, name("frame", phase, pass).c_str());
        }
    }

    // Spot checks against the wiring in docs/Display.md: numeric 1 segment A
    // is bit 2 of the last wire byte; slot 0 is the first seven bytes.
    expectEqual(frames[Display::kBlinkOn][0][6] & 0x04U, 0x04U,
                "level-1 numeric 1 lit in pass 0");
    expectEqual(frames[Display::kBlinkOn][1][6], 0U, "level-1 numeric 1 dark in pass 1");
    expectEqual(frames[Display::kBlinkOn][3][6], 0U, "level-1 numeric 1 dark in pass 3");
    // Matrix row 0 (logical) is wire slot 4; the numeric-only pass has no matrix bytes.
    expectEqual(frames[Display::kBlinkOn][3][4 * Display::kBytesPerSlot + 4], 0U,
                "matrix dark in the numeric-only pass");
    expectEqual(frames[Display::kBlinkOn][2][4 * Display::kBytesPerSlot + 4], 0xFFU,
                "full matrix row lit in pass 2");
    expectEqual(frames[Display::kBlinkOff][2][4 * Display::kBytesPerSlot + 4], 0xFEU,
                "blinking column 1 dark in the off phase");
}

} // namespace

int main()
{
    testPassTable();
    testPassElementsFollowTheTable();
    testUnlitElementsStayOff();
    testBlinkPhases();
    testEncodePassesIsEncodePcbOfEachPass();
    return Test::finish("DisplayPasses");
}
