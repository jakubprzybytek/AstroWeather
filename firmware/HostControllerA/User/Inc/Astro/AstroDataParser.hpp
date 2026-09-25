#pragma once

#include <Astro/AstroData.hpp>

#include <cstdint>

namespace HostController {

enum class AstroParseStatus : uint8_t
{
    Success,
    InvalidArgument,
    Malformed,
    UnsupportedProtocol,
    UnsupportedBoard,
    MissingRecord,
    InvalidDisplay,
    InvalidTime,
    InvalidTemperature,
    InvalidMatrix,
    Truncated,
};

AstroParseStatus parseAstroData(const uint8_t* data, uint32_t length,
                                AstroData& output);

} // namespace HostController
