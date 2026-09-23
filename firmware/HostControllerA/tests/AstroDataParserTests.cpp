#include <HostController/AstroDataParser.hpp>

#include <cstdlib>
#include <iostream>
#include <string>

namespace {

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

void expect(bool condition, const char* name)
{
    if (!condition) {
        std::cerr << name << " failed\n";
        std::exit(EXIT_FAILURE);
    }
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

HostController::AstroParseStatus parse(const std::string& payload,
                                       HostController::AstroData& data)
{
    return HostController::parseAstroData(
        reinterpret_cast<const uint8_t*>(payload.data()),
        static_cast<uint32_t>(payload.size()), data);
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

} // namespace

int main()
{
    testValidPayload();
    testMalformedTemperatureRejected();
    testUnknownKeysAreIgnored();
    testServerTime();
    std::cout << "AstroDataParser tests passed\n";
    return EXIT_SUCCESS;
}
