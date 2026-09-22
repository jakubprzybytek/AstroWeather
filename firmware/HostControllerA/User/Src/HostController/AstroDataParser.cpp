#include <HostController/AstroDataParser.hpp>

#include <Display/DisplayTypes.hpp>

#include <cmath>
#include <cstring>

namespace HostController {
namespace {

constexpr uint32_t kLineCapacity = 96U;
constexpr uint32_t kBlockFieldCount = 10U;

bool readLine(const uint8_t* data, uint32_t length, uint32_t& offset,
              char* line, uint32_t capacity, bool& complete)
{
    if (offset >= length)
    {
        return false;
    }

    uint32_t count = 0U;
    complete = false;
    while (offset < length)
    {
        const char value = static_cast<char>(data[offset++]);
        if (value == '\n')
        {
            complete = true;
            break;
        }
        if (value == '\r')
        {
            continue;
        }
        if (count + 1U >= capacity)
        {
            while (offset < length && data[offset++] != '\n') {}
            return false;
        }
        line[count++] = value;
    }
    line[count] = '\0';
    return count != 0U || complete;
}

bool splitRecord(char* line, const char*& key, const char*& value)
{
    char* separator = std::strchr(line, '=');
    if (separator == nullptr)
    {
        return false;
    }
    *separator = '\0';
    key = line;
    value = separator + 1;
    return true;
}

bool valueEquals(const char* value, const char* expected)
{
    return std::strcmp(value, expected) == 0;
}

bool parseUnsigned(const char* text, uint32_t& result)
{
    if (*text == '\0')
    {
        return false;
    }
    uint32_t value = 0U;
    for (const char* cursor = text; *cursor != '\0'; ++cursor)
    {
        if (*cursor < '0' || *cursor > '9')
        {
            return false;
        }
        value = value * 10U + static_cast<uint32_t>(*cursor - '0');
    }
    result = value;
    return true;
}

bool parseTime(const char* text, uint8_t& hour, uint8_t& minute)
{
    if (std::strlen(text) != 5U || text[2] != ':' ||
        text[0] < '0' || text[0] > '9' || text[1] < '0' || text[1] > '9' ||
        text[3] < '0' || text[3] > '9' || text[4] < '0' || text[4] > '9')
    {
        return false;
    }
    hour = static_cast<uint8_t>((text[0] - '0') * 10 + text[1] - '0');
    minute = static_cast<uint8_t>((text[3] - '0') * 10 + text[4] - '0');
    return true;
}

bool parseTemperature(const char* text, float& result)
{
    if (*text == '\0')
    {
        return false;
    }
    bool negative = false;
    if (*text == '-' || *text == '+')
    {
        negative = *text == '-';
        ++text;
    }
    if (*text < '0' || *text > '9')
    {
        return false;
    }

    float value = 0.0F;
    while (*text >= '0' && *text <= '9')
    {
        value = value * 10.0F + static_cast<float>(*text - '0');
        ++text;
    }
    if (*text != '.' || text[1] < '0' || text[1] > '9' || text[2] != '\0')
    {
        return false;
    }
    value += static_cast<float>(text[1] - '0') * 0.1F;
    result = negative ? -value : value;
    return std::isfinite(result);
}

bool parseMatrix(const char* text, uint32_t& result)
{
    if (valueEquals(text, "?"))
    {
        result = 0U;
        return true;
    }
    if (std::strlen(text) != Display::kMatrixColumnCount)
    {
        return false;
    }
    result = 0U;
    for (uint8_t index = 0U; index < Display::kMatrixColumnCount; ++index)
    {
        if (text[index] == '*')
        {
            result |= 1UL << index;
        }
        else if (text[index] != '.' && text[index] != '?')
        {
            return false;
        }
    }
    return true;
}

AstroParseStatus parseNumeric(const char* text, AstroNumericValue& output,
                              uint8_t index)
{
    if (valueEquals(text, "?"))
    {
        output = {};
        return AstroParseStatus::Success;
    }
    output.available = true;
    if (index < 2U)
    {
        output.time = true;
        if (!parseTime(text, output.hour, output.minute))
        {
            return AstroParseStatus::InvalidTime;
        }
        return AstroParseStatus::Success;
    }
    output.time = false;
    if (!parseTemperature(text, output.value))
    {
        return AstroParseStatus::InvalidTemperature;
    }
    return AstroParseStatus::Success;
}

} // namespace

AstroParseStatus parseAstroData(const uint8_t* data, uint32_t length,
                                AstroData& output)
{
    if (data == nullptr || length == 0U)
    {
        return AstroParseStatus::InvalidArgument;
    }

    AstroData parsed{};
    uint32_t offset = 0U;
    char line[kLineCapacity]{};
    bool complete = false;
    uint32_t field = 0U;
    uint32_t displayIndex = 0U;
    bool protocolSeen = false;
    bool configurationSeen = false;
    bool inBlocks = false;

    while (readLine(data, length, offset, line, sizeof(line), complete))
    {
        if (!complete && offset < length)
        {
            return AstroParseStatus::Truncated;
        }
        if (line[0] == '\0')
        {
            continue;
        }

        const char* key = nullptr;
        const char* value = nullptr;
        if (!splitRecord(line, key, value))
        {
            return AstroParseStatus::Malformed;
        }

        if (!protocolSeen)
        {
            if (std::strcmp(key, "protocol") != 0)
            {
                return AstroParseStatus::MissingRecord;
            }
            if (!valueEquals(value, "1"))
            {
                return AstroParseStatus::UnsupportedProtocol;
            }
            protocolSeen = true;
            continue;
        }
        if (!configurationSeen)
        {
            if (std::strcmp(key, "configurationId") != 0 ||
                std::strlen(value) > 20U)
            {
                return AstroParseStatus::MissingRecord;
            }
            configurationSeen = true;
            continue;
        }

        if (!inBlocks)
        {
            if (std::strcmp(key, "display") != 0)
            {
                continue;
            }
            uint32_t parsedIndex = 0U;
            if (!parseUnsigned(value, parsedIndex) || parsedIndex != 0U)
            {
                return AstroParseStatus::InvalidDisplay;
            }
            displayIndex = parsedIndex;
            field = 1U;
            inBlocks = true;
            continue;
        }

        static constexpr const char* fields[kBlockFieldCount] = {
            "board", "nightId", "numeric_0", "numeric_1", "matrix_0",
            "matrix_1", "matrix_2", "matrix_3", "numeric_2", "numeric_3"};

        if (std::strcmp(key, "display") == 0)
        {
            if (field != kBlockFieldCount + 1U || displayIndex >= 5U)
            {
                return AstroParseStatus::MissingRecord;
            }
            uint32_t parsedIndex = 0U;
            if (!parseUnsigned(value, parsedIndex) || parsedIndex != displayIndex + 1U)
            {
                return AstroParseStatus::InvalidDisplay;
            }
            displayIndex = parsedIndex;
            field = 1U;
            continue;
        }
        if (field > kBlockFieldCount || std::strcmp(key, fields[field - 1U]) != 0)
        {
            bool known = false;
            for (const char* expected : fields)
            {
                if (std::strcmp(key, expected) == 0)
                {
                    known = true;
                    break;
                }
            }
            if (known)
            {
                return AstroParseStatus::MissingRecord;
            }
            continue;
        }

        AstroBoardData& board = parsed.boards[displayIndex];
        if (field == 1U)
        {
            if (!valueEquals(value, "num4x4_matrix5x21"))
            {
                return AstroParseStatus::UnsupportedBoard;
            }
            std::strncpy(board.board.data(), value, board.board.size() - 1U);
        }
        else if (field == 2U)
        {
            std::strncpy(board.nightId.data(), value, board.nightId.size() - 1U);
        }
        else if (field >= 3U && field <= 4U)
        {
            const AstroParseStatus status = parseNumeric(
                value, board.numeric[field - 3U], static_cast<uint8_t>(field - 3U));
            if (status != AstroParseStatus::Success)
            {
                return status;
            }
        }
        else if (field >= 5U && field <= 8U)
        {
            if (!parseMatrix(value, board.matrix[field - 5U]))
            {
                return AstroParseStatus::InvalidMatrix;
            }
        }
        else
        {
            const AstroParseStatus status = parseNumeric(
                value, board.numeric[field - 7U], static_cast<uint8_t>(field - 7U));
            if (status != AstroParseStatus::Success)
            {
                return status;
            }
        }
        ++field;
    }

    if (!protocolSeen || !configurationSeen || !inBlocks ||
        displayIndex != 5U || field != kBlockFieldCount + 1U)
    {
        return AstroParseStatus::Truncated;
    }
    output = parsed;
    return AstroParseStatus::Success;
}

} // namespace HostController
