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

// One matrix record's value: the single `?`, or 21 cells from `0`-`3`, `*`
// and `?`; see docs/AstroRefresh.md#display-blocks. False for anything else.
// Also used by the 'display row' console command.
bool parseMatrixRow(const char* text, AstroMatrixRow& row);

} // namespace HostController
