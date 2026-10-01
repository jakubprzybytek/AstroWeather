#pragma once

#include <Clock/CalendarDate.hpp>

#include <array>
#include <cstddef>
#include <cstdint>
#include <cstdio>

namespace HostController {

struct AstroNumericValue
{
    bool available = false;
    bool time = false;
    uint8_t hour = 0U;
    uint8_t minute = 0U;
    float value = 0.0F;
};

// One matrix row as parsed, column i in bit i: which columns are lit, their
// level in two bit-planes (level0 is bit 0 of the level, level1 bit 1, as
// Display::BoardAttributes keeps them) and which blink. A payload `0` or `?`
// is an unlit column, `1`-`3` a level, `a`-`c` levels 1-3 blinking, `*` level
// 3 blinking.
struct AstroMatrixRow
{
    uint32_t lit = 0U;
    uint32_t level0 = 0U;
    uint32_t level1 = 0U;
    uint32_t blink = 0U;
};

struct AstroBoardData
{
    std::array<char, 32> board{};
    std::array<char, 16> nightId{};
    std::array<AstroNumericValue, 4> numeric{};
    std::array<AstroMatrixRow, 5> matrix{};  // sun, moon, cloud, precipitation, aurora
};

// The UTC offset that may follow a header date and time (`Z`, `+02:00`). It
// only says which zone the wall-clock value is in; the value itself is local.
struct AstroUtcOffset
{
    bool present = false;  // the value has an offset
    int16_t minutes = 0;   // local time minus UTC, e.g. 120 for `+02:00`
};

// `+02:00` or `-03:30` (`Z` as `+00:00`), or an empty string without an offset.
inline void formatUtcOffset(const AstroUtcOffset& offset, char (&text)[8])
{
    if (!offset.present)
    {
        text[0] = '\0';
        return;
    }
    const unsigned int magnitude =
        static_cast<unsigned int>(offset.minutes < 0 ? -offset.minutes : offset.minutes);
    std::snprintf(text, sizeof(text), "%c%02u:%02u", offset.minutes < 0 ? '-' : '+',
                  magnitude / 60U, magnitude % 60U);
}

// The `time` header record: the server's local time when it rendered the
// response. Milliseconds and the UTC offset are optional.
struct AstroServerTime
{
    bool present = false;  // the payload has the record
    bool valid = false;    // present and a well-formed, in-range date and time
    bool hasMilliseconds = false;
    Calendar::DateTime value{};
    uint16_t millisecond = 0U;
    AstroUtcOffset utcOffset{};
};

// A `last...FetchTime` header record: the server's local time when it last
// fetched one source in this response, to the second.
struct AstroFetchTime
{
    bool present = false;    // the payload has the record
    bool valid = false;      // present and either `?` or a well-formed date and time
    bool available = false;  // valid and a time, not `?` (no such data on the server)
    Calendar::DateTime value{};
    AstroUtcOffset utcOffset{};
};

// The aurora feeds with a fetch-time record, in payload order: the record
// names, and the short names the log and 'status' use.
constexpr std::size_t kAuroraFeedCount = 4U;
constexpr const char* kAuroraFetchRecords[kAuroraFeedCount] = {
    "lastGfzFetchTime", "lastNoaaKpFetchTime", "lastNoaaOutlookFetchTime", "lastOvationFetchTime"};
constexpr const char* kAuroraFeedNames[kAuroraFeedCount] = {"GFZ", "NOAA Kp", "NOAA outlook",
                                                            "OVATION"};

struct AstroData
{
    AstroServerTime serverTime{};
    AstroFetchTime lastWeatherFetch{};
    std::array<AstroFetchTime, kAuroraFeedCount> lastAuroraFetch{};
    // The `refreshIntervalMinutes` header record: 60 or 360, or 0 when it is
    // absent or has any other value (the schedule then keeps its interval).
    uint16_t refreshIntervalMinutes = 0U;
    std::array<AstroBoardData, 6> boards{};
};

} // namespace HostController
