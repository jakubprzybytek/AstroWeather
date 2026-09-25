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
    if (std::strcmp(line, "astro refresh") != 0)
    {
        return CommandResult::InvalidArgument;
    }

    const HostController::RefreshRequestResult result =
        HostController::AstroDataRefreshTask::instance().requestRefresh(
            HostController::RefreshTrigger::Console);
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
