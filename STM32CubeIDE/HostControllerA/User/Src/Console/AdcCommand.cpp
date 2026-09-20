#include <Console/AdcCommand.hpp>

#include <Sensors/CurrentSenseTask.hpp>

#include <cstring>

namespace Console {

CommandResult handleAdcCommand(const char* line)
{
    if (std::strcmp(line, "adc log on") == 0) {
        CurrentSenseTask::instance().setLoggingEnabled(true);
        return CommandResult::Ok;
    }
    if (std::strcmp(line, "adc log off") == 0) {
        CurrentSenseTask::instance().setLoggingEnabled(false);
        return CommandResult::Ok;
    }
    if (std::strcmp(line, "adc display on") == 0) {
        CurrentSenseTask::instance().setDisplayEnabled(true);
        return CommandResult::Ok;
    }
    if (std::strcmp(line, "adc display off") == 0) {
        CurrentSenseTask::instance().setDisplayEnabled(false);
        return CommandResult::Ok;
    }
    return CommandResult::NotHandled;
}

} // namespace Console