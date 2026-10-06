#pragma once

#include <Display/DisplayTypes.hpp>
#include <Astro/AstroData.hpp>

#include <cstdint>

// How a parsed astro payload is drawn on the boards. Hardware-free, so it runs
// in the native tests; AstroDataRefreshTask fills the Display's boards with it
// and then submits them. See Docs/AstroRefresh.md#display-mapping.
//
// Block n goes to the board at I2C address 0x10 + n (Display/BoardChain.hpp),
// which is the local board when that is the host's own address. Each numeric
// is drawn as its payload value says: a time as HH:MM (the payload's numerics
// 0-1), a value in whole units (numerics 2-3), and a `?` as the "unavailable"
// pattern, the decimal point on all four digits; the numerics' attributes are
// reset to plain (full, no blink). All five matrix rows come from the payload
// with their levels and blinking. Row 4, the aurora, is also where the local
// board shows the refresh progress bar; a published refresh overwrites the bar
// when the local board has a block.
namespace AstroDisplayMapper {

constexpr uint8_t kPayloadMatrixRows = 5U;
// The local board's bottom row: the aurora row, and the progress bar while a
// refresh runs or its failure is shown.
constexpr uint8_t kProgressRow = 4U;

// The decimal point on the four digits, nothing in the indicator slot.
Display::NumericSegments unavailableSegments();

void mapNumeric(Display::NumericDisplay display, const HostController::AstroNumericValue& value);

// Writes a parsed row's columns, levels and blinking. Unlit columns are left
// at full level, so a later 'display' command lighting one shows it.
void mapMatrixRow(const HostController::AstroMatrixRow& row, uint8_t index,
                  Display::MatrixRow target, Display::BoardAttributes& attributes);

// Board: anything with numeric(index), matrix(row) and attributes() returning
// a Display::NumericDisplay, a Display::MatrixRow and Display::BoardAttributes,
// such as Display::DisplayBoard or Display::DisplayBoardState.
template <typename Board>
void mapBoard(const HostController::AstroBoardData& data, bool localBoard, Board& board)
{
    Display::BoardAttributes& attributes = board.attributes();
    for (uint8_t numericIndex = 0U; numericIndex < data.numeric.size(); ++numericIndex)
    {
        mapNumeric(board.numeric(numericIndex), data.numeric[numericIndex]);
        attributes.setNumericBlink(numericIndex, Display::NumericSegments{});
        attributes.setNumericLevel(numericIndex, Display::kLevelFull);
    }
    (void)localBoard;
    for (uint8_t matrixIndex = 0U; matrixIndex < kPayloadMatrixRows; ++matrixIndex)
    {
        mapMatrixRow(data.matrix[matrixIndex], matrixIndex, board.matrix(matrixIndex), attributes);
    }
}

// Boards: anything with isLocal(position) and board(position) returning boards
// as above, such as Display::Display. Only fills the boards; the caller submits
// them.
template <typename Boards>
void mapAll(const HostController::AstroData& data, Boards& boards)
{
    for (uint8_t block = 0U; block < data.boards.size(); ++block)
    {
        mapBoard(data.boards[block], boards.isLocal(block), boards.board(block));
    }
}

} // namespace AstroDisplayMapper
