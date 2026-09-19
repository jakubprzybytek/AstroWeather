#include <Console/AdcCommand.hpp>

#include <Sensors/CurrentSenseTask.hpp>

#include <cstring>

namespace Console {

CommandResult handleAdcCommand(const char* line)
{
    if (std::strcmp(line, "adc on") == 0) {
        CurrentSenseTask::instance().setLoggingEnabled(true);
        return CommandResult::Ok;
    }
    if (std::strcmp(line, "adc off") == 0) {
        CurrentSenseTask::instance().setLoggingEnabled(false);
        return CommandResult::Ok;
    }
    return CommandResult::NotHandled;
}

} // namespace Console