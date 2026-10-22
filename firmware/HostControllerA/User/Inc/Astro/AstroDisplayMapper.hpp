#pragma once

#include <Display/BoardChain.hpp>
#include <Display/DisplayTypes.hpp>
#include <Astro/AstroData.hpp>
#include <Astro/HourIndicator.hpp>

#include <cstdint>

// How a parsed astro payload is drawn on the boards. Hardware-free, so it runs
// in the native tests; AstroDataRefreshTask fills the Display's boards with it
// and then submits them. See Docs/AstroRefresh.md#display-mapping.
//
// Block n goes to the board at I2C address 0x10 + n (Display/BoardChain.hpp),
// which is the local board when that is the host's own address; a block past
// the chain's end has no board and is not drawn. Each numeric is the
// payload's display text (NumericDisplay::setText()): `?` is the "unavailable"
// pattern, the decimal point on all four digits, and text it cannot show the
// error pattern; the numerics' attributes are reset to plain (full, no blink). All five matrix rows come from the payload
// with their levels and blinking. Row 4, the aurora, is also where the local
// board shows the refresh progress bar; a published refresh overwrites the bar
// when the local board has a block. The current-hour indicator is drawn over
// the rows by mapMatrixRows (Astro/HourIndicator.hpp).
namespace AstroDisplayMapper {

constexpr uint8_t kPayloadMatrixRows = 5U;
// The local board's bottom row: the aurora row, and the progress bar while a
// refresh runs or its failure is shown.
constexpr uint8_t kProgressRow = 4U;

void mapNumeric(Display::NumericDisplay display, const HostController::AstroNumericValue& value);

// Writes a parsed row's columns, levels and blinking. Unlit columns are left
// at full level, so a later 'display' command lighting one shows it.
void mapMatrixRow(const HostController::AstroMatrixRow& row, uint8_t index,
                  Display::MatrixRow target, Display::BoardAttributes& attributes);

// Board: anything with numeric(index), matrix(row) and attributes() returning
// a Display::NumericDisplay, a Display::MatrixRow and Display::BoardAttributes,
// such as Display::DisplayBoard or Display::DisplayBoardState.
//
// Draws the five rows, with the current-hour indicator at `indicatorColumn`
// (HourIndicator::kNoColumn for none). With `skipProgressRow` row 4 is left
// alone: the local board's progress bar or refresh outcome is on it.
template <typename Board>
void mapMatrixRows(const std::array<HostController::AstroMatrixRow, kPayloadMatrixRows>& rows,
                   uint8_t indicatorColumn, bool skipProgressRow, Board& board)
{
    for (uint8_t matrixIndex = 0U; matrixIndex < kPayloadMatrixRows; ++matrixIndex)
    {
        if (skipProgressRow && matrixIndex == kProgressRow)
        {
            continue;
        }
        mapMatrixRow(HourIndicator::withIndicator(rows[matrixIndex], indicatorColumn),
                     matrixIndex, board.matrix(matrixIndex), board.attributes());
    }
}

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
    mapMatrixRows(data.matrix, HourIndicator::kNoColumn, false, board);
}

// Boards: anything with isLocal(position) and board(position) returning boards
// as above, such as Display::Display. Only fills the boards; the caller submits
// them.
template <typename Boards>
void mapAll(const HostController::AstroData& data, Boards& boards)
{
    for (uint8_t block = 0U; block < data.boards.size() && block < Display::kChainLength; ++block)
    {
        mapBoard(data.boards[block], boards.isLocal(block), boards.board(block));
    }
}

} // namespace AstroDisplayMapper
