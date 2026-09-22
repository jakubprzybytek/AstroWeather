#pragma once

#include <Console/DisplayCommand.hpp>

namespace Console {

// 'help'          - grouped index of commands
// 'help <group>'  - details and examples for one group
//
// Replies are sent from here: the first line is 'OK help ...' and the rest are
// plain text. An unknown group is answered with an ERR line listing the valid
// ones, and still reported as handled.
CommandResult handleHelpCommand(const char* line);

} // namespace Console
