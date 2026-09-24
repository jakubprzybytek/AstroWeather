#pragma once

#include <Device/Eeprom24AA04.hpp>
#include <Settings/SettingsCodec.hpp>
#include <Utils/Mutex.hpp>

#include <cstddef>

namespace Settings {

static_assert(kImageSize <= Device::Eeprom24AA04::kSize, "settings image must fit the EEPROM");
static_assert((kImageSize % Device::Eeprom24AA04::kPageSize) == 0U,
              "save() diffs whole pages, so the image must be a whole number of them");

enum class LoadResult : uint8_t {
    Ok,         // a valid image was read and applied
    Defaulted,  // chip was blank or unreadable content; values hold defaults
    ReadFailed, // the EEPROM did not respond; values hold defaults
};

// Owns the in-RAM settings and moves them to and from the EEPROM.
//
// load() is safe to call from AppVariant_Init(), before osKernelStart(): reads
// take no osDelay, and the bus mutex is uncontended at that point so acquiring
// it does not block.
class Store
{
public:
    explicit Store(Device::Eeprom24AA04& eeprom) : eeprom_(eeprom) {}

    LoadResult load();

    // Encodes the current values and writes only the 16-byte pages that differ,
    // so flipping one flag costs a single page (~5 ms) rather than all 8 pages
    // of the image. Returns HAL_OK when nothing needed writing. Safe to call
    // from more than one task: saves are serialized by the store's lock.
    HAL_StatusTypeDef save();

    Values& values() { return values_; }
    const Values& values() const { return values_; }

    // WiFi credentials are read by another task: the console writes them and
    // the WiFi task reads them when it connects. These go through the lock so
    // neither can see a half-written string. Other fields are written only by
    // the console task, except low brightness (see setLowBrightness()), so
    // callers on the console task may still read values() directly.
    void setWifiCredentials(const char* ssid, const char* password);
    void copyWifiCredentials(char* ssid, std::size_t ssidSize, char* password,
                             std::size_t passwordSize) const;
    // Low brightness is changed by the console and by switch 2 in MainLoopTask,
    // so it is set under the lock like the credentials.
    void setLowBrightness(bool enabled);
    void resetToDefaults();

    // Detail behind a Defaulted load, for logging.
    DecodeResult lastDecode() const { return lastDecode_; }

    static const char* describe(DecodeResult result);

private:
    Device::Eeprom24AA04& eeprom_;
    Values values_{};
    DecodeResult lastDecode_ = DecodeResult::Blank;
    mutable Mutex mutex_;
};

} // namespace Settings
