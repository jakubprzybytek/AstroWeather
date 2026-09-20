#include <Console/AdcCommand.hpp>

#include <Debug/LogService.hpp>
#include <Sensors/CurrentSenseTask.hpp>
#include <Settings/SettingsStore.hpp>

#include <cstring>

namespace Console {
namespace {

// Persist immediately so the setting survives the next power cycle. Only the
// pages that changed are written, so a flag toggle costs a single page.
void persist(Settings::Store* store)
{
    if (store == nullptr) {
        return;
    }
    const HAL_StatusTypeDef status = store->save();
    if (status != HAL_OK) {
        LogService::instance().logf(LogService::Level::Error,
                                    "Settings save failed status=%u",
                                    static_cast<unsigned>(status));
    }
}

} // namespace

CommandResult handleAdcCommand(const char* line, Settings::Store* store)
{
    const bool logOn = std::strcmp(line, "adc log on") == 0;
    const bool logOff = std::strcmp(line, "adc log off") == 0;
    const bool displayOn = std::strcmp(line, "adc display on") == 0;
    const bool displayOff = std::strcmp(line, "adc display off") == 0;

    if (logOn || logOff) {
        CurrentSenseTask::instance().setLoggingEnabled(logOn);
        if (store != nullptr) {
            store->values().adcLogEnabled = logOn;
        }
    } else if (displayOn || displayOff) {
        CurrentSenseTask::instance().setDisplayEnabled(displayOn);
        if (store != nullptr) {
            store->values().adcDisplayEnabled = displayOn;
        }
    } else {
        return CommandResult::NotHandled;
    }

    persist(store);
    return CommandResult::Ok;
}

} // namespace Console