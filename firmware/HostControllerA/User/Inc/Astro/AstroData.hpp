#pragma once

#include <Clock/CalendarDate.hpp>

#include <array>
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

struct AstroBoardData
{
    std::array<char, 32> board{};
    std::array<char, 16> nightId{};
    std::array<AstroNumericValue, 4> numeric{};
    std::array<uint32_t, 4> matrix{};
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

// The `lastWeatherFetchTime` header record: the server's local time when it
// last fetched the weather in this response, to the second. Specific to the
// weather source.
struct AstroWeatherFetchTime
{
    bool present = false;    // the payload has the record
    bool valid = false;      // present and either `?` or a well-formed date and time
    bool available = false;  // valid and a time, not `?` (no weather on the server)
    Calendar::DateTime value{};
    AstroUtcOffset utcOffset{};
};

struct AstroData
{
    AstroServerTime serverTime{};
    AstroWeatherFetchTime lastWeatherFetch{};
    std::array<AstroBoardData, 6> boards{};
};

} // namespace HostController
