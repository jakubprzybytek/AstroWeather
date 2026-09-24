#include <Settings/SettingsStore.hpp>

#include <cstring>

namespace Settings {

LoadResult Store::load()
{
    if (eeprom_.read(0U, current_, static_cast<uint16_t>(sizeof(current_))) != HAL_OK) {
        values_ = Values{};
        lastDecode_ = DecodeResult::Blank;
        return LoadResult::ReadFailed;
    }

    lastDecode_ = decode(current_, sizeof(current_), values_);
    return (lastDecode_ == DecodeResult::Ok) ? LoadResult::Ok : LoadResult::Defaulted;
}

HAL_StatusTypeDef Store::save()
{
    // Held across the read-compare-write, so saves from the console and from
    // MainLoopTask (switch 2) cannot interleave their page writes into an image
    // with a bad CRC.
    MutexGuard guard(mutex_);
    if (encode(values_, desired_, sizeof(desired_)) != kImageSize) {
        return HAL_ERROR;
    }

    // Read the current image so only changed pages are rewritten. If the read
    // fails, fall back to writing everything rather than skipping the save.
    const bool haveCurrent =
        eeprom_.read(0U, current_, static_cast<uint16_t>(sizeof(current_))) == HAL_OK;

    constexpr uint16_t kPage = Device::Eeprom24AA04::kPageSize;
    for (uint16_t offset = 0U; offset < kImageSize; offset = static_cast<uint16_t>(offset + kPage)) {
        if (haveCurrent && std::memcmp(&current_[offset], &desired_[offset], kPage) == 0) {
            continue;
        }
        const HAL_StatusTypeDef status = eeprom_.write(offset, &desired_[offset], kPage);
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

void Store::setApiHost(const char* host)
{
    MutexGuard guard(mutex_);
    copyBounded(values_.apiHost, sizeof(values_.apiHost), host);
}

void Store::setApiPath(const char* path)
{
    MutexGuard guard(mutex_);
    copyBounded(values_.apiPath, sizeof(values_.apiPath), path);
}

void Store::copyApiTarget(char* host, std::size_t hostSize, char* path,
                          std::size_t pathSize) const
{
    MutexGuard guard(mutex_);
    copyBounded(host, hostSize, values_.apiHost);
    copyBounded(path, pathSize, values_.apiPath);
}

void Store::setLowBrightness(bool enabled)
{
    MutexGuard guard(mutex_);
    values_.lowBrightness = enabled;
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
