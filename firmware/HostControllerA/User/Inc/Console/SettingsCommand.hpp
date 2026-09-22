#pragma once

#include <Console/DisplayCommand.hpp>

namespace Settings {
class Store;
}

namespace Console {

// 'settings show'                 - print current values, password masked
// 'settings save'                 - force a write
// 'settings defaults'             - reset to defaults and save
// 'wifi set <ssid> <password>'    - store credentials and save
// 'wifi clear'                    - drop stored credentials and save
CommandResult handleSettingsCommand(const char* line, Settings::Store* store);

} // namespace Console
