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

} // namespace

int main()
{
    testValidPayload();
    testMalformedTemperatureRejected();
    testUnknownKeysAreIgnored();
    std::cout << "AstroDataParser tests passed\n";
    return EXIT_SUCCESS;
}
