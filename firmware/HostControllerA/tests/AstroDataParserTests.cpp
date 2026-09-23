#include <HostController/AstroDataParser.hpp>

#include <Expect.hpp>

#include <string>

namespace {

using HostController::AstroParseStatus;
using Test::expect;
using Test::expectEqual;

std::string block(unsigned int index)
{
    return "display=" + std::to_string(index) +
           "\nboard=num4x4_matrix5x21\nnightId=ignored\n"
           "numeric_0=20:30\nnumeric_1=?\n"
           "matrix_0=*....................\n"
           "matrix_1=?\nmatrix_2=.....................\n"
           "matrix_3=?????????????????????\n"
           "numeric_2=18.5\nnumeric_3=-2.0\n\n";
}

std::string validPayload()
{
    std::string payload = "protocol=1\nconfigurationId=krakow\n\n";
    for (unsigned int index = 0U; index < 6U; ++index) {
        payload += block(index);
    }
    return payload;
}

HostController::AstroParseStatus parse(const std::string& payload,
                                       HostController::AstroData& data)
{
    return HostController::parseAstroData(
        reinterpret_cast<const uint8_t*>(payload.data()),
        static_cast<uint32_t>(payload.size()), data);
}

void testValidPayload()
{
    const std::string payload = validPayload();
    HostController::AstroData data{};
    const auto status = HostController::parseAstroData(
        reinterpret_cast<const uint8_t*>(payload.data()),
        static_cast<uint32_t>(payload.size()), data);
    expect(status == HostController::AstroParseStatus::Success, "valid payload");
    expect(data.boards[0].numeric[0].available, "time available");
    expect(data.boards[0].numeric[0].time, "time type");
    expect(data.boards[0].numeric[0].hour == 20U, "time hour");
    expect(!data.boards[0].numeric[1].available, "missing time");
    expect(data.boards[0].matrix[0] == 1U, "matrix first pixel");
    expect(data.boards[0].matrix[1] == 0U, "missing matrix blank");
    expect(data.boards[0].numeric[2].value > 18.4F &&
               data.boards[0].numeric[2].value < 18.6F,
           "maximum temperature");
}

void testMalformedTemperatureRejected()
{
    std::string payload = validPayload();
    const std::string from = "numeric_2=18.5";
    const std::string to = "numeric_2=18x5";
    const std::size_t position = payload.find(from);
    payload.replace(position, from.size(), to);

    HostController::AstroData data{};
    const auto status = HostController::parseAstroData(
        reinterpret_cast<const uint8_t*>(payload.data()),
        static_cast<uint32_t>(payload.size()), data);
    expect(status == HostController::AstroParseStatus::InvalidTemperature,
           "malformed temperature");
}

void testUnknownKeysAreIgnored()
{
    std::string payload = validPayload();
    payload.insert(payload.find("display=0"), "future_key=value\n");
    HostController::AstroData data{};
    const auto status = HostController::parseAstroData(
        reinterpret_cast<const uint8_t*>(payload.data()),
        static_cast<uint32_t>(payload.size()), data);
    expect(status == HostController::AstroParseStatus::Success,
           "unknown key");
}

std::string withTime(const std::string& value)
{
    std::string payload = validPayload();
    payload.insert(payload.find("\n\n") + 1U, "time=" + value + "\n");
    return payload;
}

void testServerTime()
{
    HostController::AstroData data{};
    expect(parse(validPayload(), data) == HostController::AstroParseStatus::Success,
           "payload without time");
    expect(!data.serverTime.present, "time absent");

    expect(parse(withTime("2026-09-22T23:22:45"), data) ==
               HostController::AstroParseStatus::Success,
           "payload with time");
    expect(data.serverTime.present && data.serverTime.valid, "time valid");
    const Calendar::DateTime& t = data.serverTime.value;
    expect(t.year == 2026U && t.month == 9U && t.day == 22U && t.hour == 23U &&
               t.minute == 22U && t.second == 45U,
           "time fields");

    expect(!data.serverTime.hasMilliseconds, "whole seconds");

    expect(parse(withTime("2026-09-22T23:22:45.078"), data) ==
               HostController::AstroParseStatus::Success,
           "payload with milliseconds");
    expect(data.serverTime.valid && data.serverTime.hasMilliseconds, "milliseconds present");
    expect(data.serverTime.millisecond == 78U && data.serverTime.value.second == 45U,
           "milliseconds value");

    const char* malformed[] = {"2026-09-22 23:22:45", "2026-09-22T23:22", "2026-02-30T10:00:00",
                               "2026-09-22T24:00:00", "1999-12-31T23:59:59", "2026-9-22T23:22:45",
                               "2026-09-22T23:22:45.1", "2026-09-22T23:22:45,123",
                               "2026-09-22T23:22:45.12x", "2026-09-22T23:22:45.1234"};
    for (const char* value : malformed) {
        expect(parse(withTime(value), data) == HostController::AstroParseStatus::Success,
               "a malformed time keeps the forecast");
        expect(data.serverTime.present && !data.serverTime.valid, "malformed time flagged");
    }
}

std::string withWeatherFetchTime(const std::string& value)
{
    std::string payload = withTime("2026-09-22T23:22:45.078");
    payload.insert(payload.find("\n\n") + 1U, "lastWeatherFetchTime=" + value + "\n");
    return payload;
}

void testLastWeatherFetchTime()
{
    HostController::AstroData data{};
    expect(parse(validPayload(), data) == HostController::AstroParseStatus::Success,
           "payload without lastWeatherFetchTime");
    expect(!data.lastWeatherFetch.present, "lastWeatherFetchTime absent");

    expect(parse(withWeatherFetchTime("2026-09-22T18:00:04"), data) ==
               HostController::AstroParseStatus::Success,
           "payload with lastWeatherFetchTime");
    expect(data.lastWeatherFetch.present && data.lastWeatherFetch.valid &&
               data.lastWeatherFetch.available,
           "lastWeatherFetchTime available");
    const Calendar::DateTime& t = data.lastWeatherFetch.value;
    expect(t.year == 2026U && t.month == 9U && t.day == 22U && t.hour == 18U &&
               t.minute == 0U && t.second == 4U,
           "lastWeatherFetchTime fields");
    expect(data.serverTime.valid && data.serverTime.value.hour == 23U,
           "time still parsed next to it");

    expect(parse(withWeatherFetchTime("?"), data) == HostController::AstroParseStatus::Success,
           "payload with unavailable lastWeatherFetchTime");
    expect(data.lastWeatherFetch.present && data.lastWeatherFetch.valid &&
               !data.lastWeatherFetch.available,
           "lastWeatherFetchTime unavailable");

    const char* malformed[] = {"2026-09-22T18:00:04.000", "2026-09-22 18:00:04", "2026-02-30T10:00:00",
                               "", "??"};
    for (const char* value : malformed) {
        expect(parse(withWeatherFetchTime(value), data) == HostController::AstroParseStatus::Success,
               "a malformed lastWeatherFetchTime keeps the forecast");
        expect(data.lastWeatherFetch.present && !data.lastWeatherFetch.valid &&
                   !data.lastWeatherFetch.available,
               "malformed lastWeatherFetchTime flagged");
    }
}

void testUtcOffset()
{
    HostController::AstroData data{};
    expect(parse(withTime("2026-09-22T23:22:45.078"), data) ==
               HostController::AstroParseStatus::Success,
           "time without an offset");
    expect(data.serverTime.valid && !data.serverTime.utcOffset.present, "no offset");

    struct Case
    {
        const char* value;
        int16_t minutes;
        bool hasMilliseconds;
    };
    const Case cases[] = {{"2026-09-22T23:22:45.078+02:00", 120, true},
                          {"2026-09-22T23:22:45+01:00", 60, false},
                          {"2026-09-22T23:22:45.078Z", 0, true},
                          {"2026-09-22T23:22:45-03:30", -210, false},
                          {"2026-09-22T23:22:45-00:00", 0, false},
                          {"2026-09-22T23:22:45+23:59", 1439, false}};
    for (const Case& item : cases) {
        expect(parse(withTime(item.value), data) == HostController::AstroParseStatus::Success,
               "time with an offset");
        expect(data.serverTime.valid && data.serverTime.utcOffset.present &&
                   data.serverTime.utcOffset.minutes == item.minutes,
               "offset value");
        expect(data.serverTime.hasMilliseconds == item.hasMilliseconds &&
                   data.serverTime.value.hour == 23U && data.serverTime.value.second == 45U,
               "wall-clock time kept next to the offset");
    }

    const char* malformed[] = {"2026-09-22T23:22:45+2:00", "2026-09-22T23:22:45+0200",
                               "2026-09-22T23:22:45+02", "2026-09-22T23:22:45+24:00",
                               "2026-09-22T23:22:45+02:60", "2026-09-22T23:22:45z",
                               "2026-09-22T23:22:45 +02:00", "2026-09-22T23:22:45.078+02:00x",
                               "2026-09-22T23:22:45.07+02:00", "2026-09-22T23:22:45ZZ"};
    for (const char* value : malformed) {
        expect(parse(withTime(value), data) == HostController::AstroParseStatus::Success,
               "a malformed offset keeps the forecast");
        expect(data.serverTime.present && !data.serverTime.valid, "malformed offset flagged");
    }

    expect(parse(withWeatherFetchTime("2026-09-22T18:00:04+02:00"), data) ==
               HostController::AstroParseStatus::Success,
           "lastWeatherFetchTime with an offset");
    expect(data.lastWeatherFetch.valid && data.lastWeatherFetch.available &&
               data.lastWeatherFetch.utcOffset.present &&
               data.lastWeatherFetch.utcOffset.minutes == 120 &&
               data.lastWeatherFetch.value.hour == 18U,
           "lastWeatherFetchTime offset");
    expect(parse(withWeatherFetchTime("2026-09-22T18:00:04.000+02:00"), data) ==
                   HostController::AstroParseStatus::Success &&
               !data.lastWeatherFetch.valid,
           "lastWeatherFetchTime milliseconds rejected with an offset too");

    char text[8];
    HostController::formatUtcOffset({true, 120}, text);
    expect(std::string(text) == "+02:00", "format +02:00");
    HostController::formatUtcOffset({true, -210}, text);
    expect(std::string(text) == "-03:30", "format -03:30");
    HostController::formatUtcOffset({true, 0}, text);
    expect(std::string(text) == "+00:00", "format UTC");
    HostController::formatUtcOffset({}, text);
    expect(std::string(text).empty(), "format without an offset");
}

AstroParseStatus parse(const std::string& payload)
{
    HostController::AstroData data{};
    return parse(payload, data);
}

std::string replaced(std::string payload, const std::string& from, const std::string& to)
{
    const std::size_t position = payload.find(from);
    if (position == std::string::npos) {
        Test::fail(("payload contains " + from).c_str());
        return payload;
    }
    return payload.replace(position, from.size(), to);
}

// A line holds at most 95 characters, without its line ending.
void testLineLengthLimit()
{
    const std::string key = "future_key=";
    const std::string longest = key + std::string(95U - key.size(), 'x');
    std::string payload = validPayload();
    payload.insert(payload.find("display=0"), longest + "\n");
    expectEqual(parse(payload), AstroParseStatus::Success, "95-character line accepted");

    // The line reader gives up on an over-long line, which ends the parse
    // before any block.
    payload = validPayload();
    payload.insert(payload.find("display=0"), longest + "x\n");
    expectEqual(parse(payload), AstroParseStatus::Truncated, "96-character line rejected");

    // The CR of a CRLF ending does not count towards the limit.
    payload = validPayload();
    payload.insert(payload.find("display=0"), longest + "\r\n");
    expectEqual(parse(payload), AstroParseStatus::Success, "95 characters plus CRLF accepted");
}

// CR is dropped wherever it appears, so CRLF line endings parse the same as LF.
void testCrlfLineEndings()
{
    const std::string lf = validPayload();
    std::string crlf;
    for (const char c : lf) {
        if (c == '\n') {
            crlf += '\r';
        }
        crlf += c;
    }
    HostController::AstroData data{};
    expectEqual(parse(crlf, data), AstroParseStatus::Success, "CRLF payload");
    expectEqual(data.boards[0].numeric[0].hour, 20U, "CRLF time hour");
    expectEqual(data.boards[5].matrix[0], 1U, "CRLF matrix row");
    expect(data.boards[0].numeric[3].available && data.boards[0].numeric[3].value < -1.9F &&
               data.boards[0].numeric[3].value > -2.1F,
           "CRLF temperature");
}

void testBlocksOutOfOrder()
{
    std::string payload = replaced(validPayload(), "display=1\n", "display=X\n");
    payload = replaced(payload, "display=2\n", "display=1\n");
    payload = replaced(payload, "display=X\n", "display=2\n");
    expectEqual(parse(payload), AstroParseStatus::InvalidDisplay, "blocks 0, 2, 1");

    expectEqual(parse(replaced(validPayload(), "display=0\n", "display=1\n")),
                AstroParseStatus::InvalidDisplay, "first block not 0");

    expectEqual(parse(replaced(validPayload(), "board=num4x4_matrix5x21\nnightId=ignored\n",
                               "nightId=ignored\nboard=num4x4_matrix5x21\n")),
                AstroParseStatus::MissingRecord, "records out of order");
}

void testMissingBlock()
{
    const std::string payload = validPayload();

    const std::size_t block2 = payload.find("display=2\n");
    const std::size_t block3 = payload.find("display=3\n");
    std::string withoutBlock2 = payload;
    withoutBlock2.erase(block2, block3 - block2);
    expectEqual(parse(withoutBlock2), AstroParseStatus::InvalidDisplay, "block 2 missing");

    std::string withoutLast = payload;
    withoutLast.erase(payload.find("display=5\n"));
    expectEqual(parse(withoutLast), AstroParseStatus::Truncated, "last block missing");

    expectEqual(parse(replaced(payload, "matrix_1=?\n", "")), AstroParseStatus::MissingRecord,
                "record missing");

    expectEqual(parse(payload + block(6U)), AstroParseStatus::MissingRecord, "extra block 6");
}

void testTruncatedPayload()
{
    const std::string payload = validPayload();
    const std::size_t block3 = payload.find("display=3\n");

    // Cut at a line end inside block 3, after its matrix_0 record.
    const std::size_t matrix0End = payload.find('\n', payload.find("matrix_0=", block3));
    expectEqual(parse(payload.substr(0U, matrix0End + 1U)), AstroParseStatus::Truncated,
                "cut at a line end mid-block");

    // Cut inside a line: the partial last line is parsed as it is, so the
    // result is that record's own error rather than Truncated.
    const std::size_t matrix2 = payload.find("matrix_2=", block3);
    expectEqual(parse(payload.substr(0U, matrix2 + 15U)), AstroParseStatus::InvalidMatrix,
                "cut inside a matrix row");
    const std::size_t temperature = payload.find("numeric_2=18.5", block3);
    expectEqual(parse(payload.substr(0U, temperature + 12U)), AstroParseStatus::InvalidTemperature,
                "cut inside a temperature");

    // A payload that ends with the last record but no newline is complete.
    std::string noFinalNewline = payload;
    while (!noFinalNewline.empty() && noFinalNewline.back() == '\n') {
        noFinalNewline.pop_back();
    }
    expectEqual(parse(noFinalNewline), AstroParseStatus::Success, "no final newline");

    expectEqual(parse("protocol=1\nconfigurationId=krakow\n"), AstroParseStatus::Truncated,
                "header only");
    expectEqual(parse("protocol=1\n"), AstroParseStatus::Truncated, "protocol only");

    HostController::AstroData data{};
    expectEqual(HostController::parseAstroData(nullptr, 10U, data),
                AstroParseStatus::InvalidArgument, "null payload");
    expectEqual(HostController::parseAstroData(reinterpret_cast<const uint8_t*>(payload.data()),
                                               0U, data),
                AstroParseStatus::InvalidArgument, "empty payload");
}

void testConfigurationIdLength()
{
    const std::string twenty(20U, 'c');
    expectEqual(parse(replaced(validPayload(), "configurationId=krakow",
                               "configurationId=" + twenty)),
                AstroParseStatus::Success, "20-character configurationId");
    expectEqual(parse(replaced(validPayload(), "configurationId=krakow",
                               "configurationId=" + twenty + "c")),
                AstroParseStatus::MissingRecord, "21-character configurationId");
    expectEqual(parse(replaced(validPayload(), "configurationId=krakow\n", "")),
                AstroParseStatus::MissingRecord, "configurationId missing");
}

void testProtocol()
{
    expectEqual(parse(replaced(validPayload(), "protocol=1", "protocol=2")),
                AstroParseStatus::UnsupportedProtocol, "protocol 2");
    expectEqual(parse(replaced(validPayload(), "protocol=1\n", "")),
                AstroParseStatus::MissingRecord, "protocol missing");
    expectEqual(parse(replaced(validPayload(), "protocol=1", "protocol 1")),
                AstroParseStatus::Malformed, "record without '='");
    expectEqual(parse(replaced(validPayload(), "board=num4x4_matrix5x21",
                               "board=num4x4_matrix5x20")),
                AstroParseStatus::UnsupportedBoard, "unknown board");
}

void testUnavailableValues()
{
    HostController::AstroData data{};

    // block() has matrix_1=? and an all-'?' matrix_3; both are blank rows.
    expectEqual(parse(validPayload(), data), AstroParseStatus::Success, "? rows");
    expectEqual(data.boards[2].matrix[1], 0U, "single ? row blank");
    expectEqual(data.boards[2].matrix[3], 0U, "all-? row blank");

    // A '?' inside a row is an unlit column.
    std::string payload = replaced(validPayload(), "matrix_2=.....................",
                                   "matrix_2=*?*.................*");
    expectEqual(parse(payload, data), AstroParseStatus::Success, "mixed ? row");
    expectEqual(data.boards[0].matrix[2], 0x100005U, "mixed ? row value");

    expectEqual(parse(replaced(validPayload(), "matrix_2=.....................", "matrix_2=??")),
                AstroParseStatus::InvalidMatrix, "short ? row");

    payload = replaced(validPayload(), "numeric_2=18.5", "numeric_2=?");
    payload = replaced(payload, "numeric_0=20:30", "numeric_0=?");
    expectEqual(parse(payload, data), AstroParseStatus::Success, "? numerics");
    expect(!data.boards[0].numeric[0].available, "? time unavailable");
    expect(!data.boards[0].numeric[2].available, "? temperature unavailable");
    expect(data.boards[1].numeric[2].available, "next block's temperature available");

    expectEqual(parse(replaced(validPayload(), "numeric_2=18.5", "numeric_2=??")),
                AstroParseStatus::InvalidTemperature, "?? temperature");
    expectEqual(parse(replaced(validPayload(), "numeric_0=20:30", "numeric_0=20:3")),
                AstroParseStatus::InvalidTime, "short time");
}

} // namespace

int main()
{
    testValidPayload();
    testMalformedTemperatureRejected();
    testUnknownKeysAreIgnored();
    testServerTime();
    testLastWeatherFetchTime();
    testUtcOffset();
    testLineLengthLimit();
    testCrlfLineEndings();
    testBlocksOutOfOrder();
    testMissingBlock();
    testTruncatedPayload();
    testConfigurationIdLength();
    testProtocol();
    testUnavailableValues();
    return Test::finish("AstroDataParser");
}
