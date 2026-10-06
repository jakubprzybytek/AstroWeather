#pragma once

#include <Console/DisplayCommand.hpp>

namespace Console {

// 'errors' lists the warnings and errors kept since the log was last
// cleared, oldest first; 'errors clear' empties it. Replies itself. See
// Docs/Console.md#error-log.
CommandResult handleErrorsCommand(const char* line);

// The welcome message's line on the log: how many entries and the newest.
// Sends nothing while the log is empty.
void sendErrorLogSummary();

} // namespace Console
