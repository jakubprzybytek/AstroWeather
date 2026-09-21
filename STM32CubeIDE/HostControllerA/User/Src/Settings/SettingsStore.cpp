#include <Settings/SettingsStore.hpp>

#include <cstring>

namespace Settings {

LoadResult Store::load()
{
    uint8_t image[kImageSize];
    if (eeprom_.read(0U, image, static_cast<uint16_t>(sizeof(image))) != HAL_OK) {
        values_ = Values{};
        lastDecode_ = DecodeResult::Blank;
        return LoadResult::ReadFailed;
    }

    lastDecode_ = decode(image, sizeof(image), values_);
    return (lastDecode_ == DecodeResult::Ok) ? LoadResult::Ok : LoadResult::Defaulted;
}

HAL_StatusTypeDef Store::save()
{
    uint8_t desired[kImageSize];
    if (encode(values_, desired, sizeof(desired)) != kImageSize) {
        return HAL_ERROR;
    }

    // Read the current image so only changed pages are rewritten. If the read
    // fails, fall back to writing everything rather than skipping the save.
    uint8_t current[kImageSize];
    const bool haveCurrent =
        eeprom_.read(0U, current, static_cast<uint16_t>(sizeof(current))) == HAL_OK;

    constexpr uint16_t kPage = Device::Eeprom24AA04::kPageSize;
    for (uint16_t offset = 0U; offset < kImageSize; offset = static_cast<uint16_t>(offset + kPage)) {
        if (haveCurrent && std::memcmp(&current[offset], &desired[offset], kPage) == 0) {
            continue;
        }
        const HAL_StatusTypeDef status = eeprom_.write(offset, &desired[offset], kPage);
        if (status != HAL_OK) {
            return status;
        }
    }
    return HAL_OK;
}

namespace {

void copyBounded(char* destination, std::size_t size, const char* source)
{
    if (destination == nullptr || size == 0U) {
        return;
    }
    std::strncpy(destination, (source != nullptr) ? source : "", size - 1U);
    destination[size - 1U] = '\0';
}

} // namespace

void Store::setWifiCredentials(const char* ssid, const char* password)
{
    MutexGuard guard(mutex_);
    copyBounded(values_.wifiSsid, sizeof(values_.wifiSsid), ssid);
    copyBounded(values_.wifiPassword, sizeof(values_.wifiPassword), password);
}

void Store::copyWifiCredentials(char* ssid, std::size_t ssidSize, char* password,
                                std::size_t passwordSize) const
{
    MutexGuard guard(mutex_);
    copyBounded(ssid, ssidSize, values_.wifiSsid);
    copyBounded(password, passwordSize, values_.wifiPassword);
}

void Store::resetToDefaults()
{
    MutexGuard guard(mutex_);
    values_ = Values{};
}

const char* Store::describe(DecodeResult result)
{
    switch (result) {
    case DecodeResult::Ok:
        return "ok";
    case DecodeResult::Blank:
        return "blank";
    case DecodeResult::BadMagic:
        return "bad-magic";
    case DecodeResult::BadVersion:
        return "bad-version";
    case DecodeResult::BadLength:
        return "bad-length";
    case DecodeResult::BadCrc:
        return "bad-crc";
    case DecodeResult::Truncated:
        return "truncated";
    }
    return "unknown";
}

} // namespace Settings
