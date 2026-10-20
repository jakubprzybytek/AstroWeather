#pragma once

#include <Astro/AstroData.hpp>

#include <array>
#include <cstdint>

// The current-hour indicator: on the board whose block is the current
// observing night, the current hour's column is drawn one level brighter in
// all five matrix rows. Pure, so it runs in the native tests;
// AstroDataRefreshTask finds the board and draws it through
// AstroDisplayMapper. See Docs/AstroRefresh.md#current-hour-indicator.
namespace HourIndicator {

constexpr uint8_t kNoColumn = 0xFFU;
// Column 0 is local 14:00-15:00 on the night's date, column 20 10:00-11:00 on
// the next date, as in the payload.
constexpr uint8_t kFirstColumnHour = 14U;
// A night runs from noon to noon: from 00:00 to 11:59 it is the previous
// date's.
constexpr uint8_t kNightStartHour = 12U;

struct Position
{
    std::array<char, 16> nightId{};  // YYYY-MM-DD, as the payload's nightId
    uint8_t column = kNoColumn;      // kNoColumn from 11:00 to 13:59: no column
};

// Where the indicator is at `localSeconds` (Calendar::secondsSince2000(),
// local wall-clock time). Before the first noon of 2000 there is no night.
Position at(uint32_t localSeconds);

// The level a cell at `level` is drawn at in the indicator column: one level
// brighter, but full drops to 2, so that the column shows in a full row too.
constexpr uint8_t indicatedLevel(uint8_t level)
{
    return level >= 3U ? 2U : static_cast<uint8_t>(level + 1U);
}

// `row` with column `column` at its indicated level, blinking as before. A
// column out of range leaves the row as it is.
HostController::AstroMatrixRow withIndicator(const HostController::AstroMatrixRow& row,
                                             uint8_t column);

} // namespace HourIndicator
