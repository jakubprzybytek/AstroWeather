#include <Console/LowBrightnessCommand.hpp>

#include <Debug/LogService.hpp>
#include <HostController/LowBrightness.hpp>
#include <Settings/SettingsStore.hpp>

#include <cstdio>
#include <cstring>

namespace Console {
namespace {

void persist(Settings::Store* store)
{
    if (store == nullptr) {
        return;
    }
    const HAL_StatusTypeDef status = store->save();
    if (status != HAL_OK) {
        LogService::instance().logf(LogService::Level::Error, "Settings save failed status=%u",
                                    static_cast<unsigned>(status));
    }
}

} // namespace

CommandResult handleLowBrightnessCommand(const char* line, Settings::Store* store)
{
    const bool on = std::strcmp(line, "display low on") == 0;
    const bool off = std::strcmp(line, "display low off") == 0;
    const bool show = std::strcmp(line, "display low") == 0;
    if (!on && !off && !show) {
        return CommandResult::NotHandled;
    }

    if (on || off) {
        LowBrightness::set(on);
        if (store != nullptr) {
            store->setLowBrightness(on);
            persist(store);
        }
        LogService::instance().sendLine(on ? "OK display-low=on" : "OK display-low=off");
        return CommandResult::Ok;
    }

    char message[64];
    if (store != nullptr) {
        std::snprintf(message, sizeof(message), "OK display-low=%s saved=%s",
                      LowBrightness::isEnabled() ? "on" : "off",
                      store->values().lowBrightness ? "on" : "off");
    } else {
        std::snprintf(message, sizeof(message), "OK display-low=%s",
                      LowBrightness::isEnabled() ? "on" : "off");
    }
    LogService::instance().sendLine(message);
    return CommandResult::Ok;
}

} // namespace Console
