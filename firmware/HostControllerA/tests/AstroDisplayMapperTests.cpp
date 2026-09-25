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
        data.matrix[row] = (static_cast<uint32_t>(block) << 8U) | (1UL << row) | (1UL << 20U);
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
    // Rows as parsed: '*' is a set bit, '.' and '?' clear ones, column i bit i.
    AstroBoardData data{};
    data.matrix[0] = 0x155555U;  // "*.*.*.*.*.*.*.*.*.*.*"
    data.matrix[1] = 0U;         // a whole-row "?", or all '.'
    data.matrix[2] = 0x1FFFFFU;  // all '*'
    data.matrix[3] = 0x000001U;  // '*' then '.' / '?'
    DisplayBoardState board{};
    AstroDisplayMapper::mapBoard(data, false, board);
    expectEqual(board.state().matrix[0], 0x155555U, "matrix row 0");
    expectEqual(board.state().matrix[1], 0U, "matrix row 1 all clear");
    expectEqual(board.state().matrix[2], 0x1FFFFFU, "matrix row 2 all set");
    expectEqual(board.state().matrix[3], 0x000001U, "matrix row 3");

    // Bits beyond the 21 columns are dropped by MatrixRow.
    data.matrix[0] = 0xFFFFFFFFU;
    AstroDisplayMapper::mapBoard(data, false, board);
    expectEqual(board.state().matrix[0], Display::kMatrixMask, "matrix row masked to 21 columns");
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
        expectEqual(state.matrix[row], data.matrix[row], caseName);
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
    std::string payload = "protocol=1\nconfigurationId=test\n\n";
    for (unsigned int index = 0U; index < 6U; ++index) {
        // Column `index` marks the block, so a block on the wrong board shows.
        std::string marked(21U, '.');
        marked[index] = '*';
        payload += "display=" + std::to_string(index) +
                   "\nboard=num4x4_matrix5x21\nnightId=n\n"
                   "numeric_0=20:30\nnumeric_1=?\n"
                   "matrix_0=" + marked + "\n"
                   "matrix_1=?\n"
                   "matrix_2=*.?*.?*.?*.?*.?*.?*.?\n"
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
        // '*' on, '.' and '?' off: columns 0, 3, 6, ... 18.
        expectEqual(state.matrix[2], 0x049249U, "parsed * . ? row");
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
    testProgressRow();
    testAllBlocks();
    testParsedPayload();
    return Test::finish("AstroDisplayMapper");
}
