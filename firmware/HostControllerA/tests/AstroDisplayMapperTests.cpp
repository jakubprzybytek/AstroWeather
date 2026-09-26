#include <Astro/AstroDisplayMapper.hpp>
#include <Astro/AstroDataParser.hpp>

#include <Expect.hpp>

#include <array>
#include <cstdint>
#include <string>

using Display::DisplayBoardState;
using Display::NumericSegments;
using HostController::AstroBoardData;
using HostController::AstroData;
using HostController::AstroNumericValue;
using Test::expect;
using Test::expectEqual;

namespace {

// Stands in for Display::Display: a local board and five remote slots.
struct FakeBoards
{
    DisplayBoardState localBoard{};
    std::array<DisplayBoardState, 5> remoteBoards{};

    DisplayBoardState& local() { return localBoard; }
    DisplayBoardState& remote(uint8_t index) { return remoteBoards[index]; }
};

bool sameSegments(const NumericSegments& actual, const NumericSegments& expected)
{
    return actual.slots == expected.slots;
}

// What the display's own setters draw, so these tests pin which setter the
// mapper uses rather than the digit formatting (tested in NumericDisplayTests).
NumericSegments expectedTime(uint8_t hour, uint8_t minute)
{
    NumericSegments segments{};
    Display::NumericDisplay(segments).setTime(hour, minute);
    return segments;
}

NumericSegments expectedValue(float value)
{
    NumericSegments segments{};
    Display::NumericDisplay(segments).setValue(value, 1U);
    return segments;
}

AstroNumericValue timeValue(uint8_t hour, uint8_t minute)
{
    AstroNumericValue value{};
    value.available = true;
    value.time = true;
    value.hour = hour;
    value.minute = minute;
    return value;
}

AstroNumericValue plainValue(float number)
{
    AstroNumericValue value{};
    value.available = true;
    value.value = number;
    return value;
}

// Distinct content per block, so a block drawn on the wrong board shows.
AstroBoardData blockData(uint8_t block)
{
    AstroBoardData data{};
    data.numeric[0] = timeValue(static_cast<uint8_t>(18U + block), 5U);
    data.numeric[1] = timeValue(static_cast<uint8_t>(block), static_cast<uint8_t>(40U + block));
    data.numeric[2] = plainValue(10.5F + static_cast<float>(block));
    data.numeric[3] = plainValue(-3.0F - static_cast<float>(block));
    for (uint8_t row = 0U; row < 4U; ++row) {
        const uint32_t lit = (static_cast<uint32_t>(block) << 8U) | (1UL << row) | (1UL << 20U);
        data.matrix[row] = {lit, lit, lit, 0U};  // all at level 3
    }
    return data;
}

void testUnavailablePattern()
{
    const NumericSegments unavailable = AstroDisplayMapper::unavailableSegments();
    for (uint8_t slot = 0U; slot < 4U; ++slot) {
        expectEqual(unavailable.slots[slot], Display::kSegmentDp, "unavailable digit is the DP");
    }
    expectEqual(unavailable.slots[4], 0U, "unavailable indicator slot is blank");
}

void testNumericKinds()
{
    NumericSegments segments{};
    segments.slots.fill(0x55U);
    AstroDisplayMapper::mapNumeric(Display::NumericDisplay(segments), timeValue(21U, 7U));
    expect(sameSegments(segments, expectedTime(21U, 7U)), "time drawn with setTime");
    expectEqual(segments.slots[4], 0x03U, "time lights the colon");

    AstroDisplayMapper::mapNumeric(Display::NumericDisplay(segments), timeValue(0U, 0U));
    expect(sameSegments(segments, expectedTime(0U, 0U)), "midnight drawn with setTime");

    AstroDisplayMapper::mapNumeric(Display::NumericDisplay(segments), plainValue(18.5F));
    expect(sameSegments(segments, expectedValue(18.5F)), "value drawn with one decimal");

    AstroDisplayMapper::mapNumeric(Display::NumericDisplay(segments), plainValue(-12.3F));
    expect(sameSegments(segments, expectedValue(-12.3F)), "negative value with one decimal");

    AstroDisplayMapper::mapNumeric(Display::NumericDisplay(segments), plainValue(1234.5F));
    expect(sameSegments(segments, expectedValue(1234.5F)),
           "value too large takes the display's own error pattern");
}

void testUnavailableNumeric()
{
    NumericSegments segments{};
    AstroDisplayMapper::mapNumeric(Display::NumericDisplay(segments), timeValue(12U, 0U));
    // `?` wins whatever else the value holds.
    AstroNumericValue unknownTime = timeValue(12U, 0U);
    unknownTime.available = false;
    AstroDisplayMapper::mapNumeric(Display::NumericDisplay(segments), unknownTime);
    expect(sameSegments(segments, AstroDisplayMapper::unavailableSegments()),
           "? time drawn as the unavailable pattern");

    AstroNumericValue unknownValue = plainValue(5.0F);
    unknownValue.available = false;
    segments.slots.fill(0x7FU);
    AstroDisplayMapper::mapNumeric(Display::NumericDisplay(segments), unknownValue);
    expect(sameSegments(segments, AstroDisplayMapper::unavailableSegments()),
           "? value drawn as the unavailable pattern");
    expect(!sameSegments(segments, expectedValue(1.0E6F)),
           "unavailable differs from the display error pattern");
}

void testMatrixRows()
{
    // Rows as parsed, column i bit i: the lit columns, their level planes and
    // which blink.
    AstroBoardData data{};
    data.matrix[0] = {0x155555U, 0x155555U, 0x155555U, 0U};       // "303030..." at full
    data.matrix[1] = {0U, 0U, 0U, 0U};                             // a whole-row "?"
    data.matrix[2] = {0x1FFFFFU, 0x1FFFFFU, 0x1FFFFFU, 0x1FFFFFU}; // all '*'
    data.matrix[3] = {0x000007U, 0x000005U, 0x000006U, 0U};       // "123" then off
    DisplayBoardState board{};
    board.attributes().setMatrixBlink(1, 0x1FFFFFU);  // stale, from before
    AstroDisplayMapper::mapBoard(data, false, board);
    expectEqual(board.state().matrix[0], 0x155555U, "matrix row 0");
    expectEqual(board.state().matrix[1], 0U, "matrix row 1 all clear");
    expectEqual(board.state().matrix[2], 0x1FFFFFU, "matrix row 2 all set");
    expectEqual(board.state().matrix[3], 0x000007U, "matrix row 3");

    const Display::BoardAttributes& attributes = board.attributes();
    expectEqual(attributes.matrixLevel(0, 0), 3U, "row 0 column 0 full");
    expectEqual(attributes.matrixLevel(0, 1), 3U, "unlit column left at full level");
    expectEqual(attributes.blink.matrix[1], 0U, "stale blink cleared by the row");
    expectEqual(attributes.blink.matrix[2], 0x1FFFFFU, "row 2 blinks");
    expectEqual(attributes.matrixLevel(3, 0), 1U, "row 3 column 0 level 1");
    expectEqual(attributes.matrixLevel(3, 1), 2U, "row 3 column 1 level 2");
    expectEqual(attributes.matrixLevel(3, 2), 3U, "row 3 column 2 level 3");
    expectEqual(attributes.matrixLevel(3, 3), 3U, "row 3 unlit column full");

    // Bits beyond the 21 columns are dropped.
    data.matrix[0] = {0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU, 0xFFFFFFFFU};
    AstroDisplayMapper::mapBoard(data, false, board);
    expectEqual(board.state().matrix[0], Display::kMatrixMask, "matrix row masked to 21 columns");
    expectEqual(attributes.blink.matrix[0], Display::kMatrixMask, "blink masked to 21 columns");
    expectEqual(attributes.level0.matrix[0], Display::kMatrixMask, "level plane masked");
}

void testNumericAttributesReset()
{
    AstroBoardData data = blockData(0U);
    DisplayBoardState board{};
    board.attributes().setNumericBlink(3, Display::NumericSegments{{0U, 0U, 0U, 0U, 0x03U}});
    board.attributes().setNumericLevel(1, 1U);
    AstroDisplayMapper::mapBoard(data, false, board);
    expectEqual(board.attributes().blink.numeric[3].slots[4], 0U, "numeric blink reset");
    expectEqual(board.attributes().numericLevel(1, 0, 0), 3U, "numeric level reset to full");
}

void testProgressRow()
{
    AstroBoardData data = blockData(0U);

    DisplayBoardState local{};
    local.state().matrix[4] = 0x00000FU;  // a progress bar being shown
    AstroDisplayMapper::mapBoard(data, true, local);
    expectEqual(local.state().matrix[4], 0x00000FU, "local row 4 left to the progress bar");

    DisplayBoardState remote{};
    remote.state().matrix[4] = 0x1FFFFFU;
    AstroDisplayMapper::mapBoard(data, false, remote);
    expectEqual(remote.state().matrix[4], 0U, "remote row 4 cleared");
}

void expectBoard(const DisplayBoardState& board, uint8_t block, const char* caseName)
{
    const AstroBoardData data = blockData(block);
    const auto& state = board.state();
    expect(sameSegments(state.numeric[0], expectedTime(data.numeric[0].hour, data.numeric[0].minute)),
           caseName);
    expect(sameSegments(state.numeric[1], expectedTime(data.numeric[1].hour, data.numeric[1].minute)),
           caseName);
    expect(sameSegments(state.numeric[2], expectedValue(data.numeric[2].value)), caseName);
    expect(sameSegments(state.numeric[3], expectedValue(data.numeric[3].value)), caseName);
    for (uint8_t row = 0U; row < 4U; ++row) {
        expectEqual(state.matrix[row], data.matrix[row].lit, caseName);
    }
}

void testAllBlocks()
{
    AstroData data{};
    for (uint8_t block = 0U; block < 6U; ++block) {
        data.boards[block] = blockData(block);
    }
    FakeBoards boards{};
    boards.localBoard.state().matrix[4] = 0x000007U;
    for (auto& remote : boards.remoteBoards) {
        remote.state().matrix[4] = 0x1FFFFFU;
    }

    AstroDisplayMapper::mapAll(data, boards);

    expectBoard(boards.localBoard, 0U, "block 0 on the local board");
    expectEqual(boards.localBoard.state().matrix[4], 0x000007U, "local progress row kept");
    const char* remoteCases[5] = {"block 1 on remote 0", "block 2 on remote 1",
                                  "block 3 on remote 2", "block 4 on remote 3",
                                  "block 5 on remote 4"};
    for (uint8_t slot = 0U; slot < 5U; ++slot) {
        expectBoard(boards.remoteBoards[slot], static_cast<uint8_t>(slot + 1U), remoteCases[slot]);
        expectEqual(boards.remoteBoards[slot].state().matrix[4], 0U, remoteCases[slot]);
    }
}

// Parser and mapper together, from payload characters to board bits.
void testParsedPayload()
{
    std::string payload = "protocol=2\nconfigurationId=test\n\n";
    for (unsigned int index = 0U; index < 6U; ++index) {
        // Column `index` marks the block, so a block on the wrong board shows.
        std::string marked(21U, '0');
        marked[index] = '3';
        payload += "display=" + std::to_string(index) +
                   "\nboard=num4x4_matrix5x21\nnightId=n\n"
                   "numeric_0=20:30\nnumeric_1=?\n"
                   "matrix_0=" + marked + "\n"
                   "matrix_1=?\n"
                   "matrix_2=3?1000000000000000002\n"
                   "matrix_3=?????????????????????\n"
                   "numeric_2=18.5\nnumeric_3=?\n\n";
    }
    AstroData data{};
    const auto status = HostController::parseAstroData(
        reinterpret_cast<const uint8_t*>(payload.data()),
        static_cast<uint32_t>(payload.size()), data);
    expect(status == HostController::AstroParseStatus::Success, "parsed payload");

    FakeBoards boards{};
    boards.localBoard.state().matrix[4] = 0x000003U;
    AstroDisplayMapper::mapAll(data, boards);

    for (uint8_t board = 0U; board < 6U; ++board) {
        const auto& state = (board == 0U) ? boards.localBoard.state()
                                          : boards.remoteBoards[board - 1U].state();
        expectEqual(state.matrix[0], 1UL << board, "parsed block marker on its board");
        expectEqual(state.matrix[1], 0U, "parsed whole-row ? is all off");
        // "3?1" then off, "2" at the end: columns 0, 2 and 20 lit.
        expectEqual(state.matrix[2], 0x100005U, "parsed 3 ? 1 ... 2 row");
        const Display::BoardAttributes& attributes =
            (board == 0U) ? boards.localBoard.attributes()
                          : boards.remoteBoards[board - 1U].attributes();
        expectEqual(attributes.matrixLevel(2, 0), 3U, "parsed level 3");
        expectEqual(attributes.matrixLevel(2, 2), 1U, "parsed level 1");
        expectEqual(attributes.matrixLevel(2, 20), 2U, "parsed level 2");
        expectEqual(state.matrix[3], 0U, "parsed row of ? is all off");
        expectEqual(state.matrix[4], board == 0U ? 0x000003U : 0U, "parsed row 4");
        expect(sameSegments(state.numeric[0], expectedTime(20U, 30U)), "parsed time");
        expect(sameSegments(state.numeric[1], AstroDisplayMapper::unavailableSegments()),
               "parsed ? time");
        expect(sameSegments(state.numeric[2], expectedValue(18.5F)), "parsed value");
        expect(sameSegments(state.numeric[3], AstroDisplayMapper::unavailableSegments()),
               "parsed ? value");
    }
}

} // namespace

int main()
{
    testUnavailablePattern();
    testNumericKinds();
    testUnavailableNumeric();
    testMatrixRows();
    testNumericAttributesReset();
    testProgressRow();
    testAllBlocks();
    testParsedPayload();
    return Test::finish("AstroDisplayMapper");
}
