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

} // namespace

int main()
{
    testLeapYears();
    testValidDates();
    testDayOfWeek();
    testEveryDayAdvancesByOne();

    if (failures != 0) {
        std::cerr << failures << " CalendarDate test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "CalendarDate tests passed\n";
    return EXIT_SUCCESS;
}
