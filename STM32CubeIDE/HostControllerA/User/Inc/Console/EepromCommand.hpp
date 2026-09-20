#pragma once

#include <Console/DisplayCommand.hpp>

namespace Device {
class Eeprom24AA01;
}

namespace Console {

// 'eeprom probe'              - acknowledge poll the chip
// 'eeprom dump'               - hex dump of the whole 128-byte array
// 'eeprom read <off> [len]'   - hex dump of a range
// 'eeprom write <off> <hex>'  - write a hex byte string, e.g. 'eeprom write 0 A55A01'
// 'eeprom erase'              - fill the array with 0xFF
// 'eeprom scan'               - ack-poll every address on the bus
CommandResult handleEepromCommand(const char* line, Device::Eeprom24AA01* eeprom);

} // namespace Console
