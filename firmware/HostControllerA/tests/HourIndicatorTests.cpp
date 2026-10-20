#include <Astro/HourIndicator.hpp>
#include <Clock/CalendarDate.hpp>

#include <Expect.hpp>

#include <cstdint>
#include <cstring>
#include <string>

using HostController::AstroMatrixRow;
using Test::expect;
using Test::expectEqual;

namespace {

uint32_t localTime(uint16_t year, uint8_t month, uint8_t day, uint8_t hour, uint8_t minute = 0U)
{
    return Calendar::secondsSince2000(year, month, day, hour, minute, 0U);
}

void expectPosition(uint32_t seconds, const char* nightId, uint8_t column, const std::string& what)
{
    const HourIndicator::Position position = HourIndicator::at(seconds);
    expect(std::strcmp(position.nightId.data(), nightId) == 0, (what + ": night " + nightId).c_str());
    expectEqual(position.column, column, (what + ": column").c_str());
}

uint8_t levelOf(const AstroMatrixRow& row, uint8_t column)
{
    const uint32_t bit = 1UL << column;
    if ((row.lit & bit) == 0U)
    {
        return 0U;
    }
    return static_cast<uint8_t>(((row.level0 & bit) != 0U ? 1U : 0U) |
                                ((row.level1 & bit) != 0U ? 2U : 0U));
}

// A row with `level` (0-3) in column `column`, blinking if `blink`.
AstroMatrixRow rowWith(uint8_t column, uint8_t level, bool blink = false)
{
    AstroMatrixRow row{};
    const uint32_t bit = 1UL << column;
    if (level != 0U)
    {
        row.lit |= bit;
    }
    if ((level & 1U) != 0U)
    {
        row.level0 |= bit;
    }
    if ((level & 2U) != 0U)
    {
        row.level1 |= bit;
    }
    if (blink)
    {
        row.blink |= bit;
    }
    return row;
}

void testColumns()
{
    expectPosition(localTime(2026, 10, 19, 14), "2026-10-19", 0U, "14:00");
    expectPosition(localTime(2026, 10, 19, 14, 59), "2026-10-19", 0U, "14:59");
    expectPosition(localTime(2026, 10, 19, 23, 30), "2026-10-19", 9U, "23:30");
    expectPosition(localTime(2026, 10, 20, 0), "2026-10-19", 10U, "midnight, still the night before");
    expectPosition(localTime(2026, 10, 20, 10, 59), "2026-10-19", 20U, "10:59, the last column");
}

void testNoColumnAroundNoon()
{
    expectPosition(localTime(2026, 10, 20, 11), "2026-10-19", HourIndicator::kNoColumn, "11:00");
    expectPosition(localTime(2026, 10, 20, 11, 59), "2026-10-19", HourIndicator::kNoColumn, "11:59");
    expectPosition(localTime(2026, 10, 20, 12), "2026-10-20", HourIndicator::kNoColumn,
                   "noon starts the next night");
    expectPosition(localTime(2026, 10, 20, 13, 59), "2026-10-20", HourIndicator::kNoColumn, "13:59");
}

void testNightAcrossMonthAndYear()
{
    expectPosition(localTime(2026, 11, 1, 3), "2026-10-31", 13U, "month end");
    expectPosition(localTime(2027, 1, 1, 5), "2026-12-31", 15U, "year end");
    expectPosition(localTime(2028, 3, 1, 1), "2028-02-29", 11U, "leap day");
}

void testBeforeTheFirstNight()
{
    const HourIndicator::Position position = HourIndicator::at(localTime(2000, 1, 1, 5));
    expectEqual(position.column, HourIndicator::kNoColumn, "2000-01-01 morning: no column");
    expect(position.nightId[0] == '\0', "2000-01-01 morning: no night");
}

void testIndicatedLevels()
{
    expectEqual(HourIndicator::indicatedLevel(0U), 1U, "off shows at 1");
    expectEqual(HourIndicator::indicatedLevel(1U), 2U, "1 shows at 2");
    expectEqual(HourIndicator::indicatedLevel(2U), 3U, "2 shows at 3");
    expectEqual(HourIndicator::indicatedLevel(3U), 2U, "full drops to 2");
}

void testRowColumn()
{
    for (uint8_t level = 0U; level <= 3U; ++level)
    {
        const AstroMatrixRow row = HourIndicator::withIndicator(rowWith(7U, level), 7U);
        expectEqual(levelOf(row, 7U), HourIndicator::indicatedLevel(level),
                    ("level " + std::to_string(level) + " in the column").c_str());
        expect((row.lit & (1UL << 7U)) != 0U, "the column is lit");
    }
    const AstroMatrixRow blinking = HourIndicator::withIndicator(rowWith(3U, 1U, true), 3U);
    expectEqual(levelOf(blinking, 3U), 2U, "a blinking cell is raised");
    expectEqual(blinking.blink, 1UL << 3U, "and keeps blinking");
}

void testRowElsewhere()
{
    AstroMatrixRow row = rowWith(0U, 3U);
    row.lit |= 1UL << 20U;
    row.level0 |= 1UL << 20U;
    row.blink |= 1UL << 20U;
    const AstroMatrixRow result = HourIndicator::withIndicator(row, 10U);
    expectEqual(levelOf(result, 0U), 3U, "column 0 untouched");
    expectEqual(levelOf(result, 20U), 1U, "column 20 untouched");
    expectEqual(result.blink, row.blink, "blinking untouched");
    expectEqual(levelOf(result, 10U), 1U, "the off column 10 lit at 1");

    const AstroMatrixRow none = HourIndicator::withIndicator(row, HourIndicator::kNoColumn);
    expectEqual(none.lit, row.lit, "no column: lit unchanged");
    expectEqual(none.level0, row.level0, "no column: level0 unchanged");
    expectEqual(none.level1, row.level1, "no column: level1 unchanged");
}

} // namespace

int main()
{
    testColumns();
    testNoColumnAroundNoon();
    testNightAcrossMonthAndYear();
    testBeforeTheFirstNight();
    testIndicatedLevels();
    testRowColumn();
    testRowElsewhere();
    return Test::finish("HourIndicator");
}
