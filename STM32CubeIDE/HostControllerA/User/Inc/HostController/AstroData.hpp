#pragma once

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

struct AstroData
{
    std::array<AstroBoardData, 6> boards{};
};

} // namespace HostController
