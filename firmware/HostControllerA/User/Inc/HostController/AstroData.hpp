#pragma once

#include <HostController/CalendarDate.hpp>

#include <array>
#include <cstdint>

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

// The `time` header record: the server's local time when it rendered the
// response. Servers before 2026-09-23 sent whole seconds, truncated.
struct AstroServerTime
{
    bool present = false;  // absent from payloads of servers that predate it
    bool valid = false;    // present and a well-formed, in-range date and time
    bool hasMilliseconds = false;
    Calendar::DateTime value{};
    uint16_t millisecond = 0U;
};

struct AstroData
{
    AstroServerTime serverTime{};
    std::array<AstroBoardData, 6> boards{};
};

} // namespace HostController
