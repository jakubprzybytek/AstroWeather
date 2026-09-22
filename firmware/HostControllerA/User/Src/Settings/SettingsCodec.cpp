#include <Settings/SettingsCodec.hpp>

#include <cstring>

namespace Settings {
namespace {

std::size_t boundedLength(const char* text, std::size_t maximum)
{
    std::size_t length = 0U;
    while (length < maximum && text[length] != '\0') {
        ++length;
    }
    return length;
}

void copyString(char* destination, std::size_t capacity, const uint8_t* value, std::size_t length)
{
    const std::size_t copied = (length < (capacity - 1U)) ? length : (capacity - 1U);
    std::memcpy(destination, value, copied);
    destination[copied] = '\0';
}

bool appendRecord(uint8_t* payload, std::size_t capacity, std::size_t& used, Tag tag,
                  const uint8_t* value, std::size_t valueSize)
{
    if (valueSize > 0xFEU || (used + 2U + valueSize) > capacity) {
        return false;
    }
    payload[used++] = static_cast<uint8_t>(tag);
    payload[used++] = static_cast<uint8_t>(valueSize);
    std::memcpy(&payload[used], value, valueSize);
    used += valueSize;
    return true;
}

void applyRecord(uint8_t tag, const uint8_t* value, std::size_t length, Values& values)
{
    switch (tag) {
    case static_cast<uint8_t>(Tag::AdcFlags):
        if (length >= 1U) {
            values.adcLogEnabled = (value[0] & kAdcFlagLog) != 0U;
            values.adcDisplayEnabled = (value[0] & kAdcFlagDisplay) != 0U;
        }
        break;
    case static_cast<uint8_t>(Tag::ClockTrim):
        if (length >= 4U) {
            values.clockTrimPpm = static_cast<int32_t>(
                (static_cast<uint32_t>(value[0]) << 24U) | (static_cast<uint32_t>(value[1]) << 16U) |
                (static_cast<uint32_t>(value[2]) << 8U) | static_cast<uint32_t>(value[3]));
        }
        break;
    case static_cast<uint8_t>(Tag::ClockFlags):
        if (length >= 1U) {
            values.clockDisplayEnabled = (value[0] & kClockFlagDisplay) != 0U;
        }
        break;
    case static_cast<uint8_t>(Tag::WifiSsid):
        copyString(values.wifiSsid, sizeof(values.wifiSsid), value, length);
        break;
    case static_cast<uint8_t>(Tag::WifiPassword):
        copyString(values.wifiPassword, sizeof(values.wifiPassword), value, length);
        break;
    default:
        // Unknown tag, so an older build can still read a chip written by a
        // newer one. The record is skipped, not an error.
        break;
    }
}

} // namespace

uint16_t crc16(const uint8_t* data, std::size_t size)
{
    // CRC-16/CCITT-FALSE: init 0xFFFF, poly 0x1021, no reflection, no final xor.
    uint16_t crc = 0xFFFFU;
    for (std::size_t index = 0U; index < size; ++index) {
        crc ^= static_cast<uint16_t>(static_cast<uint16_t>(data[index]) << 8U);
        for (uint8_t bit = 0U; bit < 8U; ++bit) {
            if ((crc & 0x8000U) != 0U) {
                crc = static_cast<uint16_t>(static_cast<uint16_t>(crc << 1U) ^ 0x1021U);
            } else {
                crc = static_cast<uint16_t>(crc << 1U);
            }
        }
    }
    return crc;
}

std::size_t encode(const Values& values, uint8_t* image, std::size_t size)
{
    if (image == nullptr || size < kImageSize) {
        return 0U;
    }

    uint8_t payload[kMaxPayloadSize] = {};
    std::size_t used = 0U;

    const uint8_t flags = static_cast<uint8_t>((values.adcLogEnabled ? kAdcFlagLog : 0U) |
                                               (values.adcDisplayEnabled ? kAdcFlagDisplay : 0U));
    if (!appendRecord(payload, sizeof(payload), used, Tag::AdcFlags, &flags, 1U)) {
        return 0U;
    }

    // The clock records are written only when they differ from the default,
    // so an untouched clock costs no space.
    if (values.clockTrimPpm != 0) {
        const uint32_t trim = static_cast<uint32_t>(values.clockTrimPpm);
        const uint8_t bytes[4] = {
            static_cast<uint8_t>(trim >> 24U), static_cast<uint8_t>(trim >> 16U),
            static_cast<uint8_t>(trim >> 8U), static_cast<uint8_t>(trim),
        };
        if (!appendRecord(payload, sizeof(payload), used, Tag::ClockTrim, bytes, sizeof(bytes))) {
            return 0U;
        }
    }
    if (!values.clockDisplayEnabled) {
        const uint8_t clockFlags = 0U;
        if (!appendRecord(payload, sizeof(payload), used, Tag::ClockFlags, &clockFlags, 1U)) {
            return 0U;
        }
    }

    // An empty string writes no record at all, which is what keeps an
    // unconfigured WiFi from costing any space.
    const std::size_t ssidLength = boundedLength(values.wifiSsid, kMaxSsidLength);
    if (ssidLength != 0U &&
        !appendRecord(payload, sizeof(payload), used, Tag::WifiSsid,
                      reinterpret_cast<const uint8_t*>(values.wifiSsid), ssidLength)) {
        return 0U;
    }

    const std::size_t passwordLength = boundedLength(values.wifiPassword, kMaxPasswordLength);
    if (passwordLength != 0U &&
        !appendRecord(payload, sizeof(payload), used, Tag::WifiPassword,
                      reinterpret_cast<const uint8_t*>(values.wifiPassword), passwordLength)) {
        return 0U;
    }

    image[0] = kMagic0;
    image[1] = kMagic1;
    image[4] = kContainerVersion;
    image[5] = static_cast<uint8_t>(used);
    std::memcpy(&image[kHeaderSize], payload, used);
    std::memset(&image[kHeaderSize + used], 0xFF, kImageSize - kHeaderSize - used);

    const uint16_t crc = crc16(&image[4], 2U + used);
    image[2] = static_cast<uint8_t>(crc >> 8U);
    image[3] = static_cast<uint8_t>(crc & 0xFFU);
    return kImageSize;
}

DecodeResult decode(const uint8_t* image, std::size_t size, Values& values)
{
    // Every exit path below leaves a usable set of values.
    values = Values{};

    if (image == nullptr || size < kImageSize) {
        return DecodeResult::BadLength;
    }
    if (image[0] == 0xFFU && image[1] == 0xFFU) {
        return DecodeResult::Blank;
    }
    if (image[0] != kMagic0 || image[1] != kMagic1) {
        return DecodeResult::BadMagic;
    }
    if (image[4] != kContainerVersion) {
        return DecodeResult::BadVersion;
    }

    const std::size_t payloadLength = image[5];
    if (payloadLength > kMaxPayloadSize) {
        return DecodeResult::BadLength;
    }

    const uint16_t stored =
        static_cast<uint16_t>((static_cast<uint16_t>(image[2]) << 8U) | image[3]);
    if (crc16(&image[4], 2U + payloadLength) != stored) {
        return DecodeResult::BadCrc;
    }

    const uint8_t* payload = &image[kHeaderSize];
    std::size_t offset = 0U;
    while (offset < payloadLength) {
        const uint8_t tag = payload[offset];
        if (tag == static_cast<uint8_t>(Tag::End)) {
            break;
        }
        if ((offset + 2U) > payloadLength) {
            values = Values{};
            return DecodeResult::Truncated;
        }
        const std::size_t valueLength = payload[offset + 1U];
        if ((offset + 2U + valueLength) > payloadLength) {
            values = Values{};
            return DecodeResult::Truncated;
        }
        applyRecord(tag, &payload[offset + 2U], valueLength, values);
        offset += 2U + valueLength;
    }
    return DecodeResult::Ok;
}

} // namespace Settings
