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

// Splits "display [<target>] <sub> <a> <b> ..." into the target ("0x12" or
// "all"; empty when the line has none), the subcommand and up to four
// arguments after it. Returns the argument count.
uint8_t splitArguments(const char* line, char (&target)[8], char (&sub)[16], char (&args)[4][32])
{
    std::memset(target, 0, sizeof(target));
    std::memset(sub, 0, sizeof(sub));
    std::memset(args, 0, sizeof(args));
    const char* rest = line + 8U;  // after "display "
    while (*rest == ' ') {
        ++rest;
    }
    const std::size_t length = std::strcspn(rest, " ");
    if (std::strncmp(rest, "0x", 2U) == 0 || (length == 3U && std::strncmp(rest, "all", 3U) == 0)) {
        // Too long for any address: kept as one that names no board.
        std::memcpy(target, rest, length < sizeof(target) ? length : 2U);
        rest += length;
    }
    const int found =
        std::sscanf(rest, "%15s %31s %31s %31s %31s", sub, args[0], args[1], args[2], args[3]);
    return static_cast<uint8_t>(found > 0 ? found - 1 : 0);
}

// A board a command acts on: the local board, or the remote one at chain
// position `position`.
struct Target {
    Display::DisplayBoard* board;
    uint16_t address;
    uint8_t position;
    bool local;
};
using Targets = std::array<Target, Display::kChainLength + 1U>;

Target localTarget(Display::Display& display)
{
    return Target{&display.local(), display.localAddress(), Display::kNotInChain, true};
}

// The boards `target` names: none is the local board, "0x1N" the board at
// that address (the local one at the host's own address), "all" every board,
// the local one included. Returns the count, 0 for a target naming no board.
uint8_t resolveTargets(Display::Display& display, const char* target, Targets& targets)
{
    if (*target == '\0') {
        targets[0] = localTarget(display);
        return 1U;
    }
    if (std::strcmp(target, "all") == 0) {
        uint8_t count = 0U;
        for (uint8_t position = 0U; position < Display::kChainLength; ++position) {
            if (display.isLocal(position)) {
                targets[count++] = localTarget(display);
            } else if (Display::DisplayBoard* board = display.remoteBoard(position)) {
                targets[count++] = Target{board, Display::chainAddress(position), position, false};
            }
        }
        if (!display.localInChain()) {
            targets[count++] = localTarget(display);
        }
        return count;
    }
    unsigned int address = 0U;
    char extra = '\0';
    if (std::sscanf(target, "0x%x%c", &address, &extra) != 1 || address > 0x7FU) {
        return 0U;
    }
    if (address == display.localAddress()) {
        targets[0] = localTarget(display);
        return 1U;
    }
    const uint8_t position = Display::chainPosition(static_cast<uint16_t>(address));
    Display::DisplayBoard* board = display.remoteBoard(position);
    if (board == nullptr) {
        return 0U;
    }
    targets[0] = Target{board, static_cast<uint16_t>(address), position, false};
    return 1U;
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
// sixteen digits, and blinking at every level next to the same level steady:
// the levels and blinking of everything at once, to judge by eye.
//
// Numerics: each digit's decimal point blinks at the digit's level, the rest of
// the digit is steady, and the colons blink at full level. Matrix: rows 0, 2
// and 4 are steady, row 1 blinks throughout, and row 3 blinks every other
// column.
void testPattern(Display::DisplayBoard& board)
{
    Display::BoardAttributes& attributes = board.attributes();
    for (uint8_t n = 0U; n < Display::kNumericDisplayCount; ++n) {
        board.numeric(n).setSegments(kAllSegments);
        Display::NumericSegments blinking = kColon;
        for (uint8_t digit = 0U; digit < 4U; ++digit) {
            const uint8_t position = static_cast<uint8_t>(n * 4U + digit);
            Display::NumericSegments mask{};
            mask.slots[digit] = 0xFFU;
            attributes.setNumericLevel(n, mask,
                                       static_cast<uint8_t>((position * 3U + 7U) / 15U));
            blinking.slots[digit] = Display::kSegmentDp;
        }
        Display::NumericSegments indicators{};
        indicators.slots[4] = 0x07U;
        attributes.setNumericLevel(n, indicators, Display::kLevelFull);
        attributes.setNumericBlink(n, blinking);
    }
    for (uint8_t r = 0U; r < Display::kMatrixRowCount; ++r) {
        HostController::AstroMatrixRow parsed{};
        char cells[Display::kMatrixColumnCount + 1U] = {};
        for (uint8_t c = 0U; c < Display::kMatrixColumnCount; ++c) {
            const uint8_t level = static_cast<uint8_t>((c * 3U + 10U) / 20U);
            const bool blinks = r == 1U || (r == 3U && (c % 2U) == 0U);
            // The payload's alphabet: 'a'-'c' are levels 1-3 blinking.
            cells[c] = static_cast<char>(blinks && level != 0U ? 'a' + level - 1U : '0' + level);
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

// Applies one content, blink or level subcommand to `board`'s buffered state.
CommandResult apply(Display::DisplayBoard& board, const char* sub, uint8_t count,
                    char (&args)[4][32])
{
    CommandResult result = CommandResult::InvalidArgument;
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
    return result;
}

} // namespace

CommandResult handleDisplayCommand(const char* line, Display::Display* display)
{
    if (std::strncmp(line, "display ", 8U) != 0) {
        return CommandResult::NotHandled;
    }
    char target[8];
    char sub[16];
    char args[4][32];
    const uint8_t count = splitArguments(line, target, sub, args);
    if (display == nullptr) {
        return CommandResult::Unavailable;
    }
    Targets targets{};
    const uint8_t targetCount = resolveTargets(*display, target, targets);
    if (targetCount == 0U) {
        return CommandResult::InvalidArgument;
    }

    if (std::strcmp(sub, "passes") == 0) {
        // The I2C protocol has no message for the pass table.
        if (targetCount != 1U || !targets[0].local) {
            LogService::instance().sendLine("ERR unsupported-remote");
            return CommandResult::Ok;
        }
        return passes(*targets[0].board, count, args);  // replies itself
    }
    // A bad argument fails on the first board, before any board is changed.
    for (uint8_t i = 0U; i < targetCount; ++i) {
        const CommandResult result = apply(*targets[i].board, sub, count, args);
        if (result != CommandResult::Ok) {
            return result;
        }
    }

    if (target[0] == '\0') {
        display->submitLocal();
        LogService::instance().sendLine("OK display");
        return CommandResult::Ok;
    }
    // A board that does not take it keeps the change in its buffer, which the
    // next astro refresh sends with the forecast over it.
    char unreachable[6U * Display::kChainLength + 1U] = {};
    std::size_t used = 0U;
    for (uint8_t i = 0U; i < targetCount; ++i) {
        bool reached = true;
        if (targets[i].local) {
            display->submitLocal();
        } else {
            reached = display->submitRemote(targets[i].position);
        }
        if (!reached && used < sizeof(unreachable)) {
            used += static_cast<std::size_t>(
                std::snprintf(&unreachable[used], sizeof(unreachable) - used, " 0x%02X",
                              static_cast<unsigned>(targets[i].address)));
        }
    }
    char message[64];
    if (std::strcmp(target, "all") == 0) {
        std::snprintf(message, sizeof(message), "OK display all%s%s",
                      used > 0U ? "; unreachable" : "", unreachable);
    } else {
        std::snprintf(message, sizeof(message), "%s 0x%02X",
                      used > 0U ? "ERR display-unreachable" : "OK display",
                      static_cast<unsigned>(targets[0].address));
    }
    LogService::instance().sendLine(message);
    return CommandResult::Ok;
}

} // namespace Console
