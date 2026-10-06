#pragma once

#include <Console/DisplayCommand.hpp>

namespace Console {

// 'help'                    - every command's usage and summary, by group
// 'help <group>'            - details and examples for one group
// 'help <group> <command>'  - one command, e.g. 'help display show'
// 'help all'                - every group in full
//
// Replies are sent from here, paced so a long one fits through the log
// queue: the first line is 'OK help ...' and the rest are plain text. An
// unknown group or command is answered with an ERR line and still reported as
// handled.
CommandResult handleHelpCommand(const char* line);

} // namespace Console
