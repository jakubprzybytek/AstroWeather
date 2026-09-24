#pragma once

#include <Console/DisplayCommand.hpp>

namespace Settings {
class Store;
}

namespace Console {

// 'display low on|off' sets low brightness and saves it; 'display low' shows
// the state in use and the saved one, which differ after a switch 2 press.
// Replies itself on success. `store` may be null, in which case the change
// applies but is not persisted. HostController only.
CommandResult handleLowBrightnessCommand(const char* line, Settings::Store* store);

} // namespace Console
