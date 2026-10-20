#include <Astro/HourIndicator.hpp>

#include <Clock/CalendarDate.hpp>
#include <Display/DisplayTypes.hpp>

#include <cstdio>

namespace HourIndicator {

Position at(uint32_t localSeconds)
{
    Position position{};
    constexpr uint32_t kNightStart = kNightStartHour * 3600U;
    if (localSeconds < kNightStart)
    {
        return position;
    }
    // The night's date is the date twelve hours earlier.
    const Calendar::DateTime night = Calendar::fromSecondsSince2000(localSeconds - kNightStart);
    std::snprintf(position.nightId.data(), position.nightId.size(), "%04u-%02u-%02u",
                  static_cast<unsigned int>(night.year), static_cast<unsigned int>(night.month),
                  static_cast<unsigned int>(night.day));
    const uint8_t hour = Calendar::fromSecondsSince2000(localSeconds).hour;
    const uint8_t column = static_cast<uint8_t>((hour + 24U - kFirstColumnHour) % 24U);
    if (column < Display::kMatrixColumnCount)
    {
        position.column = column;
    }
    return position;
}

HostController::AstroMatrixRow withIndicator(const HostController::AstroMatrixRow& row,
                                             uint8_t column)
{
    if (column >= Display::kMatrixColumnCount)
    {
        return row;
    }
    const uint32_t bit = 1UL << column;
    uint8_t level = 0U;
    if ((row.lit & bit) != 0U)
    {
        level = static_cast<uint8_t>(((row.level0 & bit) != 0U ? 1U : 0U) |
                                     ((row.level1 & bit) != 0U ? 2U : 0U));
    }
    const uint8_t shown = indicatedLevel(level);
    HostController::AstroMatrixRow result = row;
    result.lit |= bit;
    result.level0 = (result.level0 & ~bit) | ((shown & 1U) != 0U ? bit : 0U);
    result.level1 = (result.level1 & ~bit) | ((shown & 2U) != 0U ? bit : 0U);
    return result;
}

} // namespace HourIndicator
