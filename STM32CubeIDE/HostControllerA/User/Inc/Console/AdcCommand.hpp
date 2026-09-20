#pragma once

#include <Console/DisplayCommand.hpp>

namespace Settings {
class Store;
}

namespace Console {

// `store` may be null, in which case the toggle applies but is not persisted.
CommandResult handleAdcCommand(const char* line, Settings::Store* store);

} // namespace Console