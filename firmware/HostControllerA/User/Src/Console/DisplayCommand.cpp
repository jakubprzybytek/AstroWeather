#include <Console/DisplayCommand.hpp>

#include <Astro/AstroDataParser.hpp>
#include <Astro/AstroDisplayMapper.hpp>
#include <Debug/LogService.hpp>

#include <cstdio>
#include <cstring>
#include <cstdint>

namespace Console {
namespace {

constexpr Display::NumericSegments kColon{{0U, 0U, 0U, 0U, 0x03U}};
constexpr Display::NumericSegments kAllSegments{{0xFFU, 0xFFU, 0xFFU, 0xFFU, 0x07U}};

// A signed decimal with up to four digits before the point and up to three
// after: "12.34", "-45", "0.5". The precision is the number of decimals.
bool parseNumber(const char* text, int16_t& mantissa, uint8_t& precision)
{
    const bool negative = *text == '-';
    if (negative || *text == '+') {
        ++text;
    }
    int32_t value = 0;
    uint8_t digits = 0U;
    while (*text >= '0' && *text <= '9') {
        value = value * 10 + (*text - '0');
        ++digits;
        ++text;
    }
    if (digits == 0U || digits > 4U) {
        return false;
    }
    precision = 0U;
    if (*text == '.') {
        ++text;
        while (*text >= '0' && *text <= '9') {
            value = value * 10 + (*text - '0');
            ++precision;
            ++text;
        }
        if (precision == 0U || precision > Display::kMaxPrecision) {
            return false;
        }
    }
    if (*text != '\0' || value > 32767) {
        return false;
    }
    mantissa = static_cast<int16_t>(negative ? -value : value);
    return true;
}

bool parseIndex(const char* text, unsigned int limit, uint8_t& index)
{
    unsigned int value = 0U;
    char extra = '\0';
    if (std::sscanf(text, "%u%c", &value, &extra) != 1 || value >= limit) {
        return false;
    }
    index = static_cast<uint8_t>(value);
    return true;
}

// Splits "display <sub> <a> <b> ..." into up to four arguments after the
// subcommand. Returns the argument count.
uint8_t splitArguments(const char* line, char (&sub)[16], char (&args)[4][32])
{
    std::memset(sub, 0, sizeof(sub));
    std::memset(args, 0, sizeof(args));
    return static_cast<uint8_t>(
        std::sscanf(line, "display %15s %31s %31s %31s %31s", sub, args[0], args[1], args[2],
                    args[3]) -
        1);
}

CommandResult show(Display::DisplayBoard& board, const char* index, const char* value)
{
    uint8_t n = 0U;
    if (!parseIndex(index, Display::kNumericDisplayCount, n)) {
        return CommandResult::InvalidArgument;
    }
    Display::NumericDisplay numeric = board.numeric(n);
    unsigned int hour = 0U;
    unsigned int minute = 0U;
    char extra = '\0';
    int16_t mantissa = 0;
    uint8_t precision = 0U;
    if (std::strcmp(value, "blank") == 0) {
        numeric.setBlank();
    } else if (std::strcmp(value, "?") == 0) {
        numeric.setSegments(AstroDisplayMapper::unavailableSegments());
    } else if (std::sscanf(value, "%2u:%2u%c", &hour, &minute, &extra) == 2 &&
               std::strlen(value) == 5U) {
        numeric.setTime(static_cast<uint8_t>(hour), static_cast<uint8_t>(minute));
    } else if (parseNumber(value, mantissa, precision)) {
        numeric.setFixed(mantissa, precision);
    } else {
        return CommandResult::InvalidArgument;
    }
    return CommandResult::Ok;
}

CommandResult row(Display::DisplayBoard& board, const char* index, const char* cells)
{
    uint8_t r = 0U;
    if (!parseIndex(index, Display::kMatrixRowCount, r)) {
        return CommandResult::InvalidArgument;
    }
    // The payload's alphabet, plus '.' for off; a short row is padded with off.
    char padded[Display::kMatrixColumnCount + 1U];
    std::memset(padded, '0', Display::kMatrixColumnCount);
    padded[Display::kMatrixColumnCount] = '\0';
    const std::size_t length = std::strlen(cells);
    if (length > Display::kMatrixColumnCount) {
        return CommandResult::InvalidArgument;
    }
    for (std::size_t i = 0U; i < length; ++i) {
        padded[i] = cells[i] == '.' ? '0' : cells[i];
    }
    HostController::AstroMatrixRow parsed{};
    if (!HostController::parseMatrixRow(padded, parsed)) {
        return CommandResult::InvalidArgument;
    }
    AstroDisplayMapper::mapMatrixRow(parsed, r, board.matrix(r), board.attributes());
    return CommandResult::Ok;
}

CommandResult blink(Display::DisplayBoard& board, const char* index, const char* what)
{
    uint8_t n = 0U;
    if (!parseIndex(index, Display::kNumericDisplayCount, n)) {
        return CommandResult::InvalidArgument;
    }
    if (std::strcmp(what, "off") == 0) {
        board.attributes().setNumericBlink(n, Display::NumericSegments{});
    } else if (std::strcmp(what, "colon") == 0) {
        board.attributes().setNumericBlink(n, kColon);
    } else if (std::strcmp(what, "all") == 0) {
        board.attributes().setNumericBlink(n, kAllSegments);
    } else {
        return CommandResult::InvalidArgument;
    }
    return CommandResult::Ok;
}

CommandResult level(Display::DisplayBoard& board, const char* index, const char* value)
{
    uint8_t n = 0U;
    uint8_t l = 0U;
    if (!parseIndex(index, Display::kNumericDisplayCount, n) ||
        !parseIndex(value, Display::kLevelCount, l)) {
        return CommandResult::InvalidArgument;
    }
    board.attributes().setNumericLevel(n, l);
    return CommandResult::Ok;
}

// Every element lit, the levels running 0 to 3 along the matrix and along the
// sixteen digits, colons and a few matrix columns blinking: the levels and
// blinking of everything at once, to judge by eye.
void testPattern(Display::DisplayBoard& board)
{
    Display::BoardAttributes& attributes = board.attributes();
    for (uint8_t n = 0U; n < Display::kNumericDisplayCount; ++n) {
        board.numeric(n).setSegments(kAllSegments);
        for (uint8_t digit = 0U; digit < 4U; ++digit) {
            const uint8_t position = static_cast<uint8_t>(n * 4U + digit);
            Display::NumericSegments mask{};
            mask.slots[digit] = 0xFFU;
            attributes.setNumericLevel(n, mask,
                                       static_cast<uint8_t>((position * 3U + 7U) / 15U));
        }
        Display::NumericSegments indicators{};
        indicators.slots[4] = 0x07U;
        attributes.setNumericLevel(n, indicators, Display::kLevelFull);
        attributes.setNumericBlink(n, kColon);
    }
    for (uint8_t r = 0U; r < Display::kMatrixRowCount; ++r) {
        HostController::AstroMatrixRow parsed{};
        char cells[Display::kMatrixColumnCount + 1U] = {};
        for (uint8_t c = 0U; c < Display::kMatrixColumnCount; ++c) {
            cells[c] = static_cast<char>('0' + (c * 3U + 10U) / 20U);
        }
        if (r == 1U || r == 3U) {
            cells[4] = cells[10] = cells[16] = '*';
        }
        HostController::parseMatrixRow(cells, parsed);
        AstroDisplayMapper::mapMatrixRow(parsed, r, board.matrix(r), attributes);
    }
}

void clear(Display::DisplayBoard& board)
{
    for (uint8_t n = 0U; n < Display::kNumericDisplayCount; ++n) {
        board.numeric(n).setBlank();
    }
    for (uint8_t r = 0U; r < Display::kMatrixRowCount; ++r) {
        board.matrix(r).setRow(0U);
    }
    board.setAttributes(Display::BoardAttributes{});
}

CommandResult passes(Display::DisplayBoard& board, uint8_t argumentCount, char (&args)[4][32])
{
    if (argumentCount == 4U) {
        std::array<uint8_t, 4> percent{};
        for (uint8_t i = 0U; i < 4U; ++i) {
            if (!parseIndex(args[i], 101U, percent[i])) {
                return CommandResult::InvalidArgument;
            }
        }
        if (!board.setPassPercent(percent)) {
            return CommandResult::InvalidArgument;
        }
    } else if (argumentCount != 0U) {
        return CommandResult::InvalidArgument;
    }
    Display::DisplayBoard::RefreshStats stats{};
    if (!board.refreshStats(stats)) {
        return CommandResult::Unavailable;
    }
    char message[64];
    std::snprintf(message, sizeof(message), "OK display passes=%u/%u/%u/%u",
                  static_cast<unsigned>(stats.passPercent[0]),
                  static_cast<unsigned>(stats.passPercent[1]),
                  static_cast<unsigned>(stats.passPercent[2]),
                  static_cast<unsigned>(stats.passPercent[3]));
    LogService::instance().sendLine(message);
    return CommandResult::Ok;
}

} // namespace

CommandResult handleDisplayCommand(const char* line, Display::Display* display)
{
    if (std::strncmp(line, "display ", 8U) != 0) {
        return CommandResult::NotHandled;
    }
    char sub[16];
    char args[4][32];
    const uint8_t count = splitArguments(line, sub, args);
    if (display == nullptr) {
        return CommandResult::Unavailable;
    }
    Display::DisplayBoard& board = display->local();

    CommandResult result = CommandResult::InvalidArgument;
    if (std::strcmp(sub, "passes") == 0) {
        return passes(board, count, args);  // replies itself
    }
    if (std::strcmp(sub, "show") == 0 && count == 2U) {
        result = show(board, args[0], args[1]);
    } else if (std::strcmp(sub, "row") == 0 && count == 2U) {
        result = row(board, args[0], args[1]);
    } else if (std::strcmp(sub, "blink") == 0 && count == 2U) {
        result = blink(board, args[0], args[1]);
    } else if (std::strcmp(sub, "level") == 0 && count == 2U) {
        result = level(board, args[0], args[1]);
    } else if (std::strcmp(sub, "test") == 0 && count == 0U) {
        testPattern(board);
        result = CommandResult::Ok;
    } else if (std::strcmp(sub, "clear") == 0 && count == 0U) {
        clear(board);
        result = CommandResult::Ok;
    }
    if (result != CommandResult::Ok) {
        return result;
    }
    display->submitLocal();
    LogService::instance().sendLine("OK display");
    return CommandResult::Ok;
}

} // namespace Console
