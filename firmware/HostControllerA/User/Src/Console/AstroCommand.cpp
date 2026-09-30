#include <Console/AstroCommand.hpp>

#include <Astro/AstroDataRefreshTask.hpp>

#include <cstring>

namespace Console {

CommandResult handleAstroCommand(const char* line)
{
    if (std::strncmp(line, "astro", 5U) != 0)
    {
        return CommandResult::NotHandled;
    }
    HostController::RefreshTrigger trigger;
    if (std::strcmp(line, "astro refresh") == 0)
    {
        trigger = HostController::RefreshTrigger::Console;
    }
    else if (std::strcmp(line, "astro test") == 0)
    {
        // One fetch of the demo forecast; the saved path is untouched.
        trigger = HostController::RefreshTrigger::Test;
    }
    else
    {
        return CommandResult::InvalidArgument;
    }

    const HostController::RefreshRequestResult result =
        HostController::AstroDataRefreshTask::instance().requestRefresh(trigger);
    if (result == HostController::RefreshRequestResult::Busy)
    {
        return CommandResult::Busy;
    }
    if (result == HostController::RefreshRequestResult::Unavailable)
    {
        return CommandResult::Unavailable;
    }
    return CommandResult::Ok;
}

} // namespace Console
