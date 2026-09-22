#pragma once

#include <cstdint>

// Calendar arithmetic for the RTC date, which the firmware tracks but does not
// display. See docs/RTC.md.
//
// The RTC stores the year as 0..99 and the weekday as 1 (Monday) to 7 (Sunday),
// and works out neither leap years nor the weekday for itself.
namespace Calendar {

// The RTC's two-digit year covers 2000..2099.
constexpr uint16_t kMinYear = 2000U;
constexpr uint16_t kMaxYear = 2099U;

constexpr bool isLeapYear(uint16_t year)
{
    // No century rule is needed in 2000..2099: 2000 is a leap year.
    return (year % 4U) == 0U;
}

constexpr uint8_t daysInMonth(uint16_t year, uint8_t month)
{
    constexpr uint8_t kDays[12] = {31U, 28U, 31U, 30U, 31U, 30U,
                                   31U, 31U, 30U, 31U, 30U, 31U};
    if (month < 1U || month > 12U) {
        return 0U;
    }
    if (month == 2U && isLeapYear(year)) {
        return 29U;
    }
    return kDays[month - 1U];
}

constexpr bool isValidDate(uint16_t year, uint8_t month, uint8_t day)
{
    return year >= kMinYear && year <= kMaxYear && month >= 1U && month <= 12U &&
           day >= 1U && day <= daysInMonth(year, month);
}

// Day of the week as the RTC numbers it: 1 = Monday .. 7 = Sunday.
// Sakamoto's method, valid for any date this function accepts.
constexpr uint8_t dayOfWeek(uint16_t year, uint8_t month, uint8_t day)
{
    constexpr uint8_t kMonthOffset[12] = {0U, 3U, 2U, 5U, 0U, 3U,
                                          5U, 1U, 4U, 6U, 2U, 4U};
    if (!isValidDate(year, month, day)) {
        return 0U;
    }
    uint16_t shifted = year;
    if (month < 3U) {
        shifted = static_cast<uint16_t>(shifted - 1U);
    }
    const uint16_t sunday0 = static_cast<uint16_t>(
        (shifted + shifted / 4U - shifted / 100U + shifted / 400U +
         kMonthOffset[month - 1U] + day) % 7U);
    // Sakamoto counts from Sunday = 0; the RTC counts from Monday = 1.
    return static_cast<uint8_t>(sunday0 == 0U ? 7U : sunday0);
}

}  // namespace Calendar
