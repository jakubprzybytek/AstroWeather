#include <Console/DisplayCommand.hpp>

#include <cstdio>
#include <cstring>
#include <cstdint>
#include <limits>

namespace Console {
namespace {

bool parseMatrixRow(const char* line, unsigned int& row, uint32_t& columns)
{
    char pixels[96] = {};
    if (std::sscanf(line, "display matrix %u %95s", &row, pixels) != 2 || pixels[0] == '\0') {
        return false;
    }

    columns = 0U;
    for (unsigned int index = 0U; pixels[index] != '\0'; ++index) {
        if (pixels[index] != '0' && pixels[index] != '1') {
            return false;
        }
        if (index < Display::kMatrixColumnCount && pixels[index] == '1') {
            columns |= 1UL << index;
        }
    }
    return true;
}

} // namespace

CommandResult handleDisplayCommand(const char* line, Display::Display* display)
{
    unsigned int index = 0U;
    int value = 0;
    unsigned int precision = 0U;
    unsigned int hour = 0U;
    unsigned int minute = 0U;
    uint32_t matrixColumns = 0U;

    if (std::strncmp(line, "display ", 8U) != 0) {
        return CommandResult::NotHandled;
    }

    if (std::sscanf(line, "display set %u %d %u", &index, &value, &precision) == 3) {
        const int32_t magnitude = value < 0 ? -static_cast<int32_t>(value) : value;
        const bool valueValid = value >= std::numeric_limits<int16_t>::min() &&
                                value <= std::numeric_limits<int16_t>::max() &&
                                value != std::numeric_limits<int16_t>::min() &&
                                (value < 0 ? magnitude <= 999 : magnitude <= 9999);
        if (display == nullptr) {
            return CommandResult::Unavailable;
        }
        if (index >= Display::kNumericDisplayCount || precision > Display::kMaxPrecision ||
            !valueValid) {
            return CommandResult::InvalidArgument;
        }
        display->local().numeric(static_cast<uint8_t>(index)).setFixed(
            static_cast<int16_t>(value), static_cast<uint8_t>(precision));
        display->submit();
        return CommandResult::Ok;
    }

    if (std::sscanf(line, "display time %u %u:%u", &index, &hour, &minute) == 3) {
        if (display == nullptr) {
            return CommandResult::Unavailable;
        }
        if (index >= Display::kNumericDisplayCount || hour > 99U || minute > 99U) {
            return CommandResult::InvalidArgument;
        }
        display->local().numeric(static_cast<uint8_t>(index)).setTime(
            static_cast<uint8_t>(hour), static_cast<uint8_t>(minute));
        display->submit();
        return CommandResult::Ok;
    }

    if (std::sscanf(line, "display blank %u", &index) == 1) {
        if (display == nullptr) {
            return CommandResult::Unavailable;
        }
        if (index >= Display::kNumericDisplayCount) {
            return CommandResult::InvalidArgument;
        }
        display->local().numeric(static_cast<uint8_t>(index)).setBlank();
        display->submit();
        return CommandResult::Ok;
    }

    if (std::sscanf(line, "display matrix %u", &index) == 1) {
        if (!parseMatrixRow(line, index, matrixColumns) || index >= Display::kMatrixRowCount) {
            return CommandResult::InvalidArgument;
        }
        if (display == nullptr) {
            return CommandResult::Unavailable;
        }
        display->local().matrix(static_cast<uint8_t>(index)).setRow(matrixColumns);
        display->submit();
        return CommandResult::Ok;
    }

    return CommandResult::InvalidArgument;
}

} // namespace Console