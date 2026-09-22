#pragma once

#include <Console/DisplayCommand.hpp>

namespace Settings {
class Store;
}

namespace Console {

// 'time show', 'time set HH:MM', 'time trim <ppm>' and 'time display on|off'.
// Replies itself on success. `store` may be null, in which case the trim and
// display toggle apply but are not persisted.
CommandResult handleTimeCommand(const char* line, Settings::Store* store);

} // namespace Console
