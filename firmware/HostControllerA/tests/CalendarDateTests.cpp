#include <HostController/CalendarDate.hpp>

#include <cstdlib>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char* caseName)
{
    if (!condition) {
        std::cerr << caseName << " failed\n";
        ++failures;
    }
}

void testLeapYears()
{
    expect(Calendar::isLeapYear(2000U), "2000 is a leap year");
    expect(Calendar::isLeapYear(2024U), "2024 is a leap year");
    expect(!Calendar::isLeapYear(2026U), "2026 is not a leap year");
    expect(Calendar::daysInMonth(2024U, 2U) == 29U, "February 2024 has 29 days");
    expect(Calendar::daysInMonth(2026U, 2U) == 28U, "February 2026 has 28 days");
    expect(Calendar::daysInMonth(2026U, 9U) == 30U, "September has 30 days");
    expect(Calendar::daysInMonth(2026U, 12U) == 31U, "December has 31 days");
    expect(Calendar::daysInMonth(2026U, 13U) == 0U, "month 13 has no days");
}

void testValidDates()
{
    expect(Calendar::isValidDate(2026U, 9U, 22U), "an ordinary date");
    expect(Calendar::isValidDate(2000U, 1U, 1U), "the first supported date");
    expect(Calendar::isValidDate(2099U, 12U, 31U), "the last supported date");
    expect(!Calendar::isValidDate(1999U, 12U, 31U), "before the RTC's range");
    expect(!Calendar::isValidDate(2100U, 1U, 1U), "after the RTC's range");
    expect(!Calendar::isValidDate(2026U, 0U, 1U), "month 0");
    expect(!Calendar::isValidDate(2026U, 9U, 31U), "September has no 31st");
    expect(!Calendar::isValidDate(2026U, 2U, 29U), "2026 has no 29 February");
    expect(Calendar::isValidDate(2024U, 2U, 29U), "2024 has a 29 February");
    expect(!Calendar::isValidDate(2026U, 9U, 0U), "day 0");
}

void testDayOfWeek()
{
    // 1 = Monday .. 7 = Sunday, as the RTC numbers them.
    expect(Calendar::dayOfWeek(2026U, 9U, 22U) == 2U, "2026-09-22 is a Tuesday");
    expect(Calendar::dayOfWeek(2026U, 9U, 21U) == 1U, "2026-09-21 is a Monday");
    expect(Calendar::dayOfWeek(2026U, 9U, 20U) == 7U, "2026-09-20 is a Sunday");
    expect(Calendar::dayOfWeek(2000U, 1U, 1U) == 6U, "2000-01-01 was a Saturday");
    expect(Calendar::dayOfWeek(2024U, 2U, 29U) == 4U, "2024-02-29 was a Thursday");
    expect(Calendar::dayOfWeek(2099U, 12U, 31U) == 4U, "2099-12-31 is a Thursday");
    expect(Calendar::dayOfWeek(2026U, 2U, 30U) == 0U, "an invalid date has no weekday");
}

void testEveryDayAdvancesByOne()
{
    uint8_t expected = Calendar::dayOfWeek(2000U, 1U, 1U);
    for (uint16_t year = Calendar::kMinYear; year <= Calendar::kMaxYear; ++year) {
        for (uint8_t month = 1U; month <= 12U; ++month) {
            for (uint8_t day = 1U; day <= Calendar::daysInMonth(year, month); ++day) {
                if (Calendar::dayOfWeek(year, month, day) != expected) {
                    std::cerr << "weekday breaks at " << year << '-' << unsigned(month) << '-'
                              << unsigned(day) << '\n';
                    expect(false, "every day advances the weekday by one");
                    return;
                }
                expected = static_cast<uint8_t>((expected % 7U) + 1U);
            }
        }
    }
}

void testSecondsSince2000()
{
    expect(Calendar::secondsSince2000(2000U, 1U, 1U, 0U, 0U, 0U) == 0U, "the epoch is 0");
    expect(Calendar::secondsSince2000(2000U, 1U, 2U, 0U, 0U, 1U) == 86401U, "a day and a second");
    expect(Calendar::daysSince2000(2001U, 1U, 1U) == 366U, "2000 has 366 days");
    // 2026-09-23 09:09:47 is 843 469 787 s after the epoch (Python's datetime).
    expect(Calendar::secondsSince2000(2026U, 9U, 23U, 9U, 9U, 47U) == 843469787U,
           "a known timestamp");
    const Calendar::DateTime last = Calendar::fromSecondsSince2000(
        Calendar::secondsSince2000(2099U, 12U, 31U, 23U, 59U, 59U));
    expect(last.year == 2099U && last.month == 12U && last.day == 31U && last.hour == 23U &&
               last.minute == 59U && last.second == 59U,
           "the last second round-trips");
}

void testEveryDayRoundTrips()
{
    uint32_t expectedDays = 0U;
    for (uint16_t year = Calendar::kMinYear; year <= Calendar::kMaxYear; ++year) {
        for (uint8_t month = 1U; month <= 12U; ++month) {
            for (uint8_t day = 1U; day <= Calendar::daysInMonth(year, month); ++day) {
                const uint32_t seconds =
                    Calendar::secondsSince2000(year, month, day, 12U, 34U, 56U);
                const Calendar::DateTime back = Calendar::fromSecondsSince2000(seconds);
                if (Calendar::daysSince2000(year, month, day) != expectedDays ||
                    back.year != year || back.month != month || back.day != day ||
                    back.hour != 12U || back.minute != 34U || back.second != 56U) {
                    std::cerr << "round trip breaks at " << year << '-' << unsigned(month)
                              << '-' << unsigned(day) << '\n';
                    expect(false, "every day round-trips through seconds");
                    return;
                }
                ++expectedDays;
            }
        }
    }
}

} // namespace

int main()
{
    testLeapYears();
    testValidDates();
    testDayOfWeek();
    testEveryDayAdvancesByOne();
    testSecondsSince2000();
    testEveryDayRoundTrips();

    if (failures != 0) {
        std::cerr << failures << " CalendarDate test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "CalendarDate tests passed\n";
    return EXIT_SUCCESS;
}
