#pragma once

#include <Display/Display.hpp>

namespace Console {

enum class CommandResult {
    NotHandled,
    Ok,
    Busy,
    Unavailable,
    InvalidArgument,
};

CommandResult handleDisplayCommand(const char* line, Display::Display* display);

} // namespace Console