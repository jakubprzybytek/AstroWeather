#pragma once

#include <Display/DisplayTypes.hpp>
#include <Astro/AstroData.hpp>

#include <cstdint>

// How a parsed astro payload is drawn on the boards. Hardware-free, so it runs
// in the native tests; AstroDataRefreshTask fills the Display's boards with it
// and then submits them. See docs/AstroRefresh.md#display-mapping.
//
// Block 0 goes to the local board, blocks 1-5 to remote slots 0-4. Each numeric
// is drawn as its payload value says: a time as HH:MM (the payload's numerics
// 0-1), a value with one decimal (numerics 2-3), and a `?` as the "unavailable"
// pattern, the decimal point on all four digits; the numerics' attributes are
// reset to plain (full, no blink). Matrix rows 0-3 come from the payload with
// their levels and blinking; row 4 is cleared on the remote boards and left
// alone on the local board, where it carries the refresh progress bar.
namespace AstroDisplayMapper {

constexpr uint8_t kPayloadMatrixRows = 4U;
// The local board's bottom row, owned by the progress bar.
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
    for (uint8_t matrixIndex = 0U; matrixIndex < kPayloadMatrixRows; ++matrixIndex)
    {
        mapMatrixRow(data.matrix[matrixIndex], matrixIndex, board.matrix(matrixIndex), attributes);
    }
    if (!localBoard)
    {
        board.matrix(kProgressRow).setRow(0U);
        attributes.setMatrixBlink(kProgressRow, 0U);
        attributes.setMatrixLevel(kProgressRow, Display::kMatrixMask, Display::kLevelFull);
    }
}

// Boards: anything with local() and remote(slot) returning boards as above,
// such as Display::Display. Only fills the boards; the caller submits them.
template <typename Boards>
void mapAll(const HostController::AstroData& data, Boards& boards)
{
    for (uint8_t boardIndex = 0U; boardIndex < data.boards.size(); ++boardIndex)
    {
        if (boardIndex == 0U)
        {
            mapBoard(data.boards[boardIndex], true, boards.local());
        }
        else
        {
            mapBoard(data.boards[boardIndex], false,
                     boards.remote(static_cast<uint8_t>(boardIndex - 1U)));
        }
    }
}

} // namespace AstroDisplayMapper
