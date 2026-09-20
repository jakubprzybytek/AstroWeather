#include <Console/SettingsCommand.hpp>

#include <Debug/LogService.hpp>
#include <Settings/SettingsStore.hpp>

#include <cstdio>
#include <cstring>

namespace Console {
namespace {

void reportSaveFailure(HAL_StatusTypeDef status)
{
    LogService::instance().logf(LogService::Level::Error, "Settings save failed status=%u",
                                static_cast<unsigned>(status));
}

CommandResult saveAndReport(Settings::Store& store, const char* what)
{
    const HAL_StatusTypeDef status = store.save();
    if (status != HAL_OK) {
        reportSaveFailure(status);
        return CommandResult::Unavailable;
    }
    char message[64];
    std::snprintf(message, sizeof(message), "OK settings-%s", what);
    LogService::instance().sendLine(message);
    return CommandResult::Ok;
}

void showSettings(const Settings::Store& store)
{
    const Settings::Values& values = store.values();
    char message[96];

    std::snprintf(message, sizeof(message), "OK settings adc-log=%s adc-display=%s",
                  values.adcLogEnabled ? "on" : "off",
                  values.adcDisplayEnabled ? "on" : "off");
    LogService::instance().sendLine(message);

    // The password is never printed back, only whether one is held and how long
    // it is. 'eeprom dump' will still show it in the clear.
    const std::size_t passwordLength = std::strlen(values.wifiPassword);
    std::snprintf(message, sizeof(message), "OK settings wifi-ssid=%s wifi-password=%s",
                  (values.wifiSsid[0] != '\0') ? values.wifiSsid : "<unset>",
                  (passwordLength != 0U) ? "<set>" : "<unset>");
    LogService::instance().sendLine(message);

    // Describes what load() found at startup, not the chip's present content,
    // so it still reads "blank" after the first save of a fresh chip.
    std::snprintf(message, sizeof(message), "OK settings boot-load=%s",
                  Settings::Store::describe(store.lastDecode()));
    LogService::instance().sendLine(message);
}

} // namespace

CommandResult handleSettingsCommand(const char* line, Settings::Store* store)
{
    const bool isSettings = std::strncmp(line, "settings", 8U) == 0;
    const bool isWifi = std::strncmp(line, "wifi", 4U) == 0;
    if (!isSettings && !isWifi) {
        return CommandResult::NotHandled;
    }
    if (store == nullptr) {
        return CommandResult::Unavailable;
    }

    if (std::strcmp(line, "settings show") == 0) {
        showSettings(*store);
        return CommandResult::Ok;
    }

    if (std::strcmp(line, "settings save") == 0) {
        return saveAndReport(*store, "save");
    }

    if (std::strcmp(line, "settings defaults") == 0) {
        store->values() = Settings::Values{};
        return saveAndReport(*store, "defaults");
    }

    if (std::strcmp(line, "wifi clear") == 0) {
        store->values().wifiSsid[0] = '\0';
        store->values().wifiPassword[0] = '\0';
        return saveAndReport(*store, "wifi-clear");
    }

    char ssid[Settings::kMaxSsidLength + 1U] = {};
    char password[Settings::kMaxPasswordLength + 1U] = {};
    // Widths are one below each buffer size and must track the limits above.
    static_assert(Settings::kMaxSsidLength == 32U, "update the sscanf width below");
    static_assert(Settings::kMaxPasswordLength == 63U, "update the sscanf width below");
    if (std::sscanf(line, "wifi set %32s %63s", ssid, password) == 2) {
        if (ssid[0] == '\0' || password[0] == '\0') {
            return CommandResult::InvalidArgument;
        }
        std::strcpy(store->values().wifiSsid, ssid);
        std::strcpy(store->values().wifiPassword, password);
        return saveAndReport(*store, "wifi-set");
    }

    return CommandResult::InvalidArgument;
}

} // namespace Console
