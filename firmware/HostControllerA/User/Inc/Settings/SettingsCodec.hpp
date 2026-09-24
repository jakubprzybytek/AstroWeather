#pragma once

#include <cstddef>
#include <cstdint>

namespace Settings {

// Layout of the settings image, stored at offset 0 of the 24AA04 EEPROM.
//
//   0x00  2  magic       'A','W'
//   0x02  2  crc16       CCITT over 0x04 .. 0x05 + payload
//   0x04  1  version     container format version
//   0x05  1  payloadLen  payload bytes that follow
//   0x06  N  payload     sequence of tag/length/value records
//
// The CRC deliberately sits before the fields it covers so that everything it
// protects is one contiguous range.
//
// The image is a fixed 256-byte region, the lower half of the 512-byte part.
// The CRC covers only the version, payloadLen and payloadLen bytes of payload,
// never the padding behind them, so the image can grow up to 256 bytes without
// a version bump: an image written at 128 bytes (up to 2026-09) is also a valid
// 256-byte one. Beyond 256 it cannot grow without one, since payloadLen is a
// single byte. The upper half of the part stays free.
//
// The payload is TLV rather than a packed struct chiefly so that adding a
// setting needs no migration (see below). It also keeps unset settings free: a
// struct would reserve the worst case for WiFi credentials (97 bytes) whether
// or not any are set, while as records an unconfigured WiFi costs nothing and
// a typical one about 40 bytes.
constexpr std::size_t kImageSize = 256U;
constexpr std::size_t kHeaderSize = 6U;
constexpr std::size_t kMaxPayloadSize = kImageSize - kHeaderSize;
static_assert(kMaxPayloadSize <= 255U, "payloadLen is one byte");

constexpr uint8_t kMagic0 = 'A';
constexpr uint8_t kMagic1 = 'W';
constexpr uint8_t kContainerVersion = 1U;

// Tag numbers are permanent. Once assigned, a tag never changes meaning, and a
// retired setting abandons its tag rather than recycling it.
//
// Adding a setting needs no version bump and no migration code: older content
// simply has no record for the new tag, and decode() leaves that field at its
// default. The version above tracks the container - header layout, CRC choice -
// not the set of settings, so it should almost never change.
//
// docs/Settings.md is the specification, and holds the authoritative tag
// registry. Follow its "Adding a new setting" checklist before editing this
// enum, and update the registry there to match.
enum class Tag : uint8_t {
    AdcFlags = 0x01,      // 1 byte: bit0 log enabled, bit1 display enabled
    ClockTrim = 0x02,     // 4 bytes: signed LSI error in ppm, big endian
    WifiSsid = 0x10,      // 1..32 bytes, not NUL terminated
    WifiPassword = 0x11,  // 1..63 bytes, not NUL terminated
    ApiHost = 0x12,       // 1..64 bytes, not NUL terminated
    ApiPath = 0x13,       // 1..64 bytes, not NUL terminated
    ClockFlags = 0x20,    // 1 byte: bit0 clock display enabled
    DisplayFlags = 0x21,  // 1 byte: bit0 low brightness
    End = 0xFF,           // an erased EEPROM reads 0xFF, so this terminates for free
};

constexpr std::size_t kMaxSsidLength = 32U;
constexpr std::size_t kMaxPasswordLength = 63U;
// The host is also bounded by the TLS SNI buffer (HTTP_SNI_MAX_SIZE, 64).
constexpr std::size_t kMaxApiHostLength = 64U;
constexpr std::size_t kMaxApiPathLength = 64U;

constexpr uint8_t kAdcFlagLog = 0x01U;
constexpr uint8_t kAdcFlagDisplay = 0x02U;

constexpr uint8_t kClockFlagDisplay = 0x01U;

constexpr uint8_t kDisplayFlagLowBrightness = 0x01U;

// Defaults here are the values the firmware uses when nothing is stored, and
// must match the task defaults they are applied to.
struct Values {
    bool adcLogEnabled = false;
    bool adcDisplayEnabled = true;
    bool clockDisplayEnabled = true;
    bool lowBrightness = false;
    int32_t clockTrimPpm = 0;
    char wifiSsid[kMaxSsidLength + 1U] = {};
    char wifiPassword[kMaxPasswordLength + 1U] = {};
    // Empty means "not saved": the fetch then uses the built-in
    // APP_ST67_HTTP_HOST / APP_ST67_HTTP_PATH.
    char apiHost[kMaxApiHostLength + 1U] = {};
    char apiPath[kMaxApiPathLength + 1U] = {};
};

enum class DecodeResult : uint8_t {
    Ok,
    Blank,        // erased chip, nothing stored yet
    BadMagic,
    BadVersion,   // written by a newer firmware than this one understands
    BadLength,
    BadCrc,
    Truncated,    // header was sound but a record ran past the payload
};

uint16_t crc16(const uint8_t* data, std::size_t size);

// Always leaves `values` fully populated: on any failure it holds the defaults,
// and fields with no record keep theirs.
DecodeResult decode(const uint8_t* image, std::size_t size, Values& values);

// Writes a whole kImageSize image, padding unused bytes to 0xFF so the result is
// deterministic and a dump of the tail looks erased. Returns the bytes written,
// or 0 if the values do not fit.
std::size_t encode(const Values& values, uint8_t* image, std::size_t size);

} // namespace Settings
