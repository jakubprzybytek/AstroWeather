#pragma once

#include <Console/DisplayCommand.hpp>

namespace Settings {
class Store;
}

namespace Console {

// 'api show', 'api host <host>', 'api path <path>' and 'api default': the
// server the astro refresh fetches from. Replies itself, errors included, so
// the specific reason reaches the user. `store` may be null, in which case
// only 'api show' works. HostController only.
CommandResult handleApiCommand(const char* line, Settings::Store* store);

} // namespace Console
