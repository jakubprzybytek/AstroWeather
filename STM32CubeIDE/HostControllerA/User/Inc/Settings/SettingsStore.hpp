#pragma once

#include <Device/Eeprom24AA01.hpp>
#include <Settings/SettingsCodec.hpp>

namespace Settings {

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
    explicit Store(Device::Eeprom24AA01& eeprom) : eeprom_(eeprom) {}

    LoadResult load();

    // Encodes the current values and writes only the 8-byte pages that differ,
    // so flipping one flag costs a single page (~5 ms) rather than the whole
    // image (~16 pages). Returns HAL_OK when nothing needed writing.
    HAL_StatusTypeDef save();

    Values& values() { return values_; }
    const Values& values() const { return values_; }

    // Detail behind a Defaulted load, for logging.
    DecodeResult lastDecode() const { return lastDecode_; }

    static const char* describe(DecodeResult result);

private:
    Device::Eeprom24AA01& eeprom_;
    Values values_{};
    DecodeResult lastDecode_ = DecodeResult::Blank;
};

} // namespace Settings
