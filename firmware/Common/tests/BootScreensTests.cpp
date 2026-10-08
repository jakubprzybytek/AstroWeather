// The boot screens every board shows after power-up (Display/BootScreens.hpp).

#include <Display/BootScreens.hpp>

#include <Expect.hpp>
#include <StubHal.hpp>

#include <array>
#include <cstdint>
#include <vector>

namespace {

using Display::LogicalBoardState;
using Test::expect;
using Test::expectEqual;

using Slots = std::array<uint8_t, Display::kSlotCount>;

constexpr uint8_t kGlyph1 = 0x06U;
constexpr uint8_t kGlyph2 = 0x5BU;
constexpr uint8_t kGlyphA = 0x77U;
constexpr uint8_t kGlyphD = 0x5EU;
constexpr uint8_t kGlyphF = 0x71U;
constexpr uint8_t kMinus = 0x40U;

// Records what show() put up and when; submit() is never expected.
class RecordingBoard : public Display::DisplayBoard {
public:
    struct Shown {
        LogicalBoardState state;
        Display::BoardAttributes attributes;
        uint32_t tick;
    };

    void submit() override { ++submits; }
    void show(const LogicalBoardState& state, const Display::BoardAttributes& attributes) override
    {
        shown.push_back({state, attributes, Stub::tick()});
    }

    std::vector<Shown> shown;
    int submits = 0;
};

bool blankNumeric(const Display::NumericSegments& numeric)
{
    return numeric.slots == Slots{};
}

void testSlotTest()
{
    for (uint8_t slot = 0U; slot < Display::kSlotCount; ++slot) {
        const LogicalBoardState state = Display::slotTestState(slot);
        Slots expected{};
        expected[slot] = (slot == 4U) ? 0x07U : 0xFFU;
        for (const Display::NumericSegments& numeric : state.numeric) {
            expect(numeric.slots == expected, "slot test: only that slot, every segment");
        }
        for (uint8_t row = 0U; row < Display::kMatrixRowCount; ++row) {
            const uint32_t expectedRow =
                (row == slot) ? Display::kMatrixMask : 0U;
            expectEqual(state.matrix[row], expectedRow, "slot test: matrix steps from the top row");
        }
    }
    const LogicalBoardState none = Display::slotTestState(Display::kSlotCount);
    expect(blankNumeric(none.numeric[0]), "slot out of range lights nothing");
    expectEqual(none.matrix[0], 0U, "slot out of range: matrix blank");
}

// Every slot is lit exactly once over the whole test.
void testSlotTestCoversBoard()
{
    LogicalBoardState lit{};
    for (uint8_t slot = 0U; slot < Display::kSlotCount; ++slot) {
        lit = lit | Display::slotTestState(slot);
    }
    for (const Display::NumericSegments& numeric : lit.numeric) {
        expect(numeric.slots == Slots{0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x07U},
               "slot test covers every digit, DP and indicator");
    }
    for (uint32_t row : lit.matrix) {
        expectEqual(row, Display::kMatrixMask, "slot test covers every matrix dot");
    }
}

void expectAddress(uint16_t address, const Slots& expected, const char* caseName)
{
    const LogicalBoardState state = Display::addressState(address);
    expect(state.numeric[0].slots == expected, caseName);
    for (uint8_t index = 1U; index < Display::kNumericDisplayCount; ++index) {
        expect(blankNumeric(state.numeric[index]), "address: numerics 2-4 blank");
    }
    for (uint32_t row : state.matrix) {
        expectEqual(row, 0U, "address: matrix blank");
    }
}

void testAddress()
{
    expectAddress(0x12U, {kGlyphA, kGlyphD, kGlyph1, kGlyph2, 0U}, "address 0x12 shows Ad12");
    expectAddress(0x2AU, {kGlyphA, kGlyphD, kGlyph2, kGlyphA, 0U}, "address 0x2A shows Ad2A");
    expectAddress(0xFFU, {kGlyphA, kGlyphD, kGlyphF, kGlyphF, 0U}, "address 0xFF shows AdFF");
    expectAddress(0U, {kGlyphA, kGlyphD, kMinus, kMinus, 0U}, "no address shows Ad--");
    expectAddress(0x100U, {kGlyphA, kGlyphD, kMinus, kMinus, 0U}, "out of range shows Ad--");
}

void testSequence()
{
    RecordingBoard board;
    board.numeric(2).setValue(42.0F, 0U);
    const LogicalBoardState before = board.state();
    const uint32_t start = Stub::tick();

    Display::showBootScreens(board, 0x11U);

    expectEqual(board.shown.size(), static_cast<std::size_t>(Display::kSlotCount + 1U),
                "five slot steps, then the address");
    for (uint8_t slot = 0U; slot < Display::kSlotCount; ++slot) {
        const auto& step = board.shown[slot];
        expect(step.state.numeric[0].slots == Display::slotTestState(slot).numeric[0].slots,
               "slot steps in order");
        expectEqual(step.tick - start, static_cast<uint32_t>(slot) * Display::kSlotTestMs,
                    "200 ms per slot");
        expectEqual(step.attributes.blink.matrix[0], 0U, "boot screens do not blink");
    }
    const auto& address = board.shown[Display::kSlotCount];
    expect(address.state.numeric[0].slots == Display::addressState(0x11U).numeric[0].slots,
           "address after the slot test");
    expectEqual(address.tick - start, 1000U, "address after 1 s");
    expectEqual(Stub::tick() - start, 3000U, "address for 2 s, 3 s in all");
    expectEqual(board.submits, 0, "boot screens never submit");
    expect(board.state().numeric[2].slots == before.numeric[2].slots,
           "the board's own state is left alone");
}

} // namespace

int main()
{
    testSlotTest();
    testSlotTestCoversBoard();
    testAddress();
    testSequence();
    return Test::finish("BootScreens");
}
