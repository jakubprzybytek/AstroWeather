#include <Settings/SettingsCodec.hpp>

#include <cstdlib>
#include <cstring>
#include <iostream>

namespace {

int failures = 0;

void expect(bool condition, const char* caseName)
{
    if (!condition) {
        std::cerr << caseName << " failed\n";
        ++failures;
    }
}

void expectResult(Settings::DecodeResult actual, Settings::DecodeResult expected,
                  const char* caseName)
{
    if (actual != expected) {
        std::cerr << caseName << " failed: expected result " << static_cast<int>(expected)
                  << ", got " << static_cast<int>(actual) << '\n';
        ++failures;
    }
}

// Builds a valid container around a hand-written payload, for the cases that
// cannot be produced by encode().
void buildImage(uint8_t* image, const uint8_t* payload, std::size_t payloadLength)
{
    std::memset(image, 0xFF, Settings::kImageSize);
    image[0] = Settings::kMagic0;
    image[1] = Settings::kMagic1;
    image[4] = Settings::kContainerVersion;
    image[5] = static_cast<uint8_t>(payloadLength);
    std::memcpy(&image[Settings::kHeaderSize], payload, payloadLength);
    const uint16_t crc = Settings::crc16(&image[4], 2U + payloadLength);
    image[2] = static_cast<uint8_t>(crc >> 8U);
    image[3] = static_cast<uint8_t>(crc & 0xFFU);
}

void testRoundTrip()
{
    Settings::Values written;
    written.adcLogEnabled = true;
    written.adcDisplayEnabled = false;
    std::strcpy(written.wifiSsid, "AstroNet");
    std::strcpy(written.wifiPassword, "correcthorsebattery");

    uint8_t image[Settings::kImageSize] = {};
    expect(Settings::encode(written, image, sizeof(image)) == Settings::kImageSize,
           "round trip encodes");

    Settings::Values read;
    expectResult(Settings::decode(image, sizeof(image), read), Settings::DecodeResult::Ok,
                 "round trip decodes");
    expect(read.adcLogEnabled, "round trip adc log");
    expect(!read.adcDisplayEnabled, "round trip adc display");
    expect(std::strcmp(read.wifiSsid, "AstroNet") == 0, "round trip ssid");
    expect(std::strcmp(read.wifiPassword, "correcthorsebattery") == 0, "round trip password");
}

void testBlankChipYieldsDefaults()
{
    uint8_t image[Settings::kImageSize];
    std::memset(image, 0xFF, sizeof(image));

    Settings::Values read;
    read.adcLogEnabled = true;  // must be replaced by the default
    expectResult(Settings::decode(image, sizeof(image), read), Settings::DecodeResult::Blank,
                 "blank chip reports blank");
    expect(!read.adcLogEnabled, "blank chip restores adc log default");
    expect(read.adcDisplayEnabled, "blank chip restores adc display default");
    expect(read.wifiSsid[0] == '\0', "blank chip leaves ssid empty");
}

void testCorruptedByteIsRejected()
{
    Settings::Values written;
    written.adcLogEnabled = true;
    uint8_t image[Settings::kImageSize] = {};
    Settings::encode(written, image, sizeof(image));

    image[Settings::kHeaderSize + 2U] ^= 0xFFU;  // flip a payload byte

    Settings::Values read;
    expectResult(Settings::decode(image, sizeof(image), read), Settings::DecodeResult::BadCrc,
                 "corrupted payload fails crc");
    expect(!read.adcLogEnabled, "failed crc falls back to defaults");
}

void testUnknownTagIsSkipped()
{
    // What an older build sees once a newer one has added a setting: an
    // unrecognised record sitting between two it does understand.
    const uint8_t payload[] = {
        0x7EU, 0x02U, 0xAAU, 0xBBU,
        static_cast<uint8_t>(Settings::Tag::AdcFlags), 0x01U,
        static_cast<uint8_t>(Settings::kAdcFlagLog | Settings::kAdcFlagDisplay),
        static_cast<uint8_t>(Settings::Tag::WifiSsid), 0x03U, 'a', 'b', 'c',
    };
    uint8_t image[Settings::kImageSize];
    buildImage(image, payload, sizeof(payload));

    Settings::Values read;
    expectResult(Settings::decode(image, sizeof(image), read), Settings::DecodeResult::Ok,
                 "unknown tag still decodes");
    expect(read.adcLogEnabled, "record after unknown tag is applied");
    expect(std::strcmp(read.wifiSsid, "abc") == 0, "later record after unknown tag is applied");
}

void testMissingRecordKeepsDefault()
{
    // The migration case: content written before WiFi existed carries only the
    // ADC record, and must leave the WiFi fields at their defaults.
    Settings::Values written;
    written.adcLogEnabled = true;
    uint8_t image[Settings::kImageSize] = {};
    Settings::encode(written, image, sizeof(image));

    Settings::Values read;
    std::strcpy(read.wifiSsid, "stale");
    expectResult(Settings::decode(image, sizeof(image), read), Settings::DecodeResult::Ok,
                 "image without wifi decodes");
    expect(read.adcLogEnabled, "adc record survives");
    expect(read.wifiSsid[0] == '\0', "absent wifi record leaves the default");
}

void testTruncatedRecordIsRejected()
{
    const uint8_t payload[] = {
        static_cast<uint8_t>(Settings::Tag::WifiSsid), 0x20U, 'a',  // claims 32 bytes, holds 1
    };
    uint8_t image[Settings::kImageSize];
    buildImage(image, payload, sizeof(payload));

    Settings::Values read;
    expectResult(Settings::decode(image, sizeof(image), read), Settings::DecodeResult::Truncated,
                 "record running past the payload is rejected");
}

void testFutureVersionIsRejected()
{
    Settings::Values written;
    uint8_t image[Settings::kImageSize] = {};
    Settings::encode(written, image, sizeof(image));
    image[4] = Settings::kContainerVersion + 1U;

    Settings::Values read;
    expectResult(Settings::decode(image, sizeof(image), read), Settings::DecodeResult::BadVersion,
                 "newer container version is rejected");
}

void testMaximumLengthFieldsFit()
{
    Settings::Values written;
    std::memset(written.wifiSsid, 'S', Settings::kMaxSsidLength);
    written.wifiSsid[Settings::kMaxSsidLength] = '\0';
    std::memset(written.wifiPassword, 'P', Settings::kMaxPasswordLength);
    written.wifiPassword[Settings::kMaxPasswordLength] = '\0';

    uint8_t image[Settings::kImageSize] = {};
    expect(Settings::encode(written, image, sizeof(image)) == Settings::kImageSize,
           "worst case credentials fit in the image");

    Settings::Values read;
    expectResult(Settings::decode(image, sizeof(image), read), Settings::DecodeResult::Ok,
                 "worst case decodes");
    expect(std::strlen(read.wifiSsid) == Settings::kMaxSsidLength, "max ssid survives");
    expect(std::strlen(read.wifiPassword) == Settings::kMaxPasswordLength,
           "max password survives");
}

void testUnconfiguredWifiCostsNothing()
{
    Settings::Values written;
    uint8_t image[Settings::kImageSize] = {};
    Settings::encode(written, image, sizeof(image));
    expect(image[5] == 3U, "unconfigured wifi writes no record");
}

} // namespace

int main()
{
    testRoundTrip();
    testBlankChipYieldsDefaults();
    testCorruptedByteIsRejected();
    testUnknownTagIsSkipped();
    testMissingRecordKeepsDefault();
    testTruncatedRecordIsRejected();
    testFutureVersionIsRejected();
    testMaximumLengthFieldsFit();
    testUnconfiguredWifiCostsNothing();

    if (failures != 0) {
        std::cerr << failures << " SettingsCodec test(s) failed\n";
        return EXIT_FAILURE;
    }
    std::cout << "SettingsCodec tests passed\n";
    return EXIT_SUCCESS;
}
