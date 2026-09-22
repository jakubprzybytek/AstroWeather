#pragma once

#include <Console/DisplayCommand.hpp>

namespace Device {
class Eeprom24AA04;
}

namespace Settings {
class Store;
}

namespace Console {

// 'status' - one-screen summary: firmware, uptime, heap, stats, EEPROM,
// settings, WiFi, the last astro refresh, and which remote boards answer.
//
// Replies are sent from here: 'OK status' then plain lines. Any of the pointers
// may be null (the DisplayController variant has none of them), in which case
// that line reports the part as not present.
CommandResult handleStatusCommand(const char* line, Display::Display* display,
                                  Device::Eeprom24AA04* eeprom, Settings::Store* settings);

} // namespace Console
