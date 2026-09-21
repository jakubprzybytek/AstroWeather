#include <Console/SettingsCommand.hpp>

#include <Debug/LogService.hpp>
#include <Settings/SettingsStore.hpp>
#if defined(FIRMWARE_VARIANT_HostController)
#include <HostController/AstroDataRefreshTask.hpp>
#endif

#include <cstdarg>
#include <cstddef>
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

enum class ArgumentStatus : uint8_t { Ok, Missing, TooLong, Unterminated };

// Splits the next argument off `cursor`. An argument is either a run of
// non-space characters or a double-quoted string, which may contain spaces -
// SSIDs and passphrases often do. There is no escape for a quote character.
ArgumentStatus nextArgument(const char*& cursor, char* out, std::size_t size)
{
    while (*cursor == ' ') {
        ++cursor;
    }
    if (*cursor == '\0') {
        out[0] = '\0';
        return ArgumentStatus::Missing;
    }
    std::size_t length = 0U;
    if (*cursor == '"') {
        ++cursor;
        while (*cursor != '\0' && *cursor != '"') {
            if (length + 1U >= size) {
                return ArgumentStatus::TooLong;
            }
            out[length++] = *cursor++;
        }
        if (*cursor != '"') {
            return ArgumentStatus::Unterminated;
        }
        ++cursor;
    } else {
        while (*cursor != '\0' && *cursor != ' ') {
            if (length + 1U >= size) {
                return ArgumentStatus::TooLong;
            }
            out[length++] = *cursor++;
        }
    }
    out[length] = '\0';
    return ArgumentStatus::Ok;
}

void reply(const char* format, ...)
{
    char text[128];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    LogService::instance().sendLine(text);
}

// Starts a connection test by running a refresh, which connects with the stored
// credentials and reports any failure in plain words. HostController only.
void startWifiTest(const char* ssid)
{
#if defined(FIRMWARE_VARIANT_HostController)
    using namespace HostController;
    const RefreshRequestResult result =
        AstroDataRefreshTask::instance().requestRefresh(RefreshTrigger::WifiTest);
    if (result == RefreshRequestResult::Accepted) {
        reply("Testing the connection to '%s' now; the result follows in a few seconds.", ssid);
    } else if (result == RefreshRequestResult::Busy) {
        reply("A refresh is already running; run 'wifi test' once it has finished.");
    } else {
        reply("The WiFi task is not ready yet; run 'wifi test' in a moment.");
    }
#else
    (void)ssid;
#endif
}

// 'wifi set <ssid> [password]'. Replies are sent from here, so the specific
// reason reaches the user instead of a generic invalid-argument.
CommandResult handleWifiSet(const char* arguments, Settings::Store& store)
{
    const char* cursor = arguments;
    // One spare byte beyond each limit, so an over-long value is measured and
    // reported rather than silently cut off.
    char ssid[Settings::kMaxSsidLength + 2U] = {};
    char password[Settings::kMaxPasswordLength + 2U] = {};
    char extra[2] = {};

    const ArgumentStatus ssidStatus = nextArgument(cursor, ssid, sizeof(ssid));
    if (ssidStatus == ArgumentStatus::Missing) {
        reply("ERR wifi-set: missing SSID. Usage: wifi set <ssid> [password]; "
              "quote values containing spaces.");
        return CommandResult::Ok;
    }
    if (ssidStatus == ArgumentStatus::Unterminated) {
        reply("ERR wifi-set: the SSID's opening quote has no closing quote.");
        return CommandResult::Ok;
    }
    if (ssidStatus == ArgumentStatus::TooLong) {
        reply("ERR wifi-set: SSID must be 1-%u characters; this one is longer.",
              static_cast<unsigned>(Settings::kMaxSsidLength));
        return CommandResult::Ok;
    }
    const ArgumentStatus passwordStatus = nextArgument(cursor, password, sizeof(password));
    if (passwordStatus == ArgumentStatus::Unterminated) {
        reply("ERR wifi-set: the password's opening quote has no closing quote.");
        return CommandResult::Ok;
    }
    if (passwordStatus == ArgumentStatus::TooLong) {
        reply("ERR wifi-set: WPA2 password must be 8-%u characters; this one is longer.",
              static_cast<unsigned>(Settings::kMaxPasswordLength));
        return CommandResult::Ok;
    }
    if (nextArgument(cursor, extra, sizeof(extra)) != ArgumentStatus::Missing) {
        reply("ERR wifi-set: too many arguments. Quote an SSID or password that contains "
              "spaces, e.g. wifi set \"My Network\" \"my pass phrase\"");
        return CommandResult::Ok;
    }

    const std::size_t ssidLength = std::strlen(ssid);
    const std::size_t passwordLength = std::strlen(password);
    if (ssidLength == 0U || ssidLength > Settings::kMaxSsidLength) {
        reply("ERR wifi-set: SSID must be 1-%u characters, got %u.",
              static_cast<unsigned>(Settings::kMaxSsidLength),
              static_cast<unsigned>(ssidLength));  // an empty quoted SSID lands here
        return CommandResult::Ok;
    }
    // WPA2 passphrases are 8-63 characters. No password at all means an open
    // network, which is allowed.
    if (passwordLength != 0U &&
        (passwordLength < 8U || passwordLength > Settings::kMaxPasswordLength)) {
        reply("ERR wifi-set: WPA2 password must be 8-%u characters, got %u. "
              "Omit it only for an open network.",
              static_cast<unsigned>(Settings::kMaxPasswordLength),
              static_cast<unsigned>(passwordLength));
        return CommandResult::Ok;
    }

    store.setWifiCredentials(ssid, password);
    std::memset(password, 0, sizeof(password));
    const HAL_StatusTypeDef status = store.save();
    if (status != HAL_OK) {
        reportSaveFailure(status);
        return CommandResult::Unavailable;
    }
    reply("OK wifi-set ssid='%s' %s; saved.", ssid,
          (passwordLength != 0U) ? "password=<set>" : "no password (open network)");
    startWifiTest(ssid);
    return CommandResult::Ok;
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
        store->resetToDefaults();
        return saveAndReport(*store, "defaults");
    }

    if (std::strcmp(line, "wifi clear") == 0) {
        store->setWifiCredentials("", "");
        return saveAndReport(*store, "wifi-clear");
    }

    if (std::strcmp(line, "wifi test") == 0) {
        const Settings::Values& values = store->values();
        if (values.wifiSsid[0] == '\0') {
            reply("ERR wifi-test: no credentials stored. Set them with "
                  "'wifi set <ssid> <password>'.");
            return CommandResult::Ok;
        }
        reply("OK wifi-test");
        startWifiTest(values.wifiSsid);
        return CommandResult::Ok;
    }

    if (std::strcmp(line, "wifi set") == 0 || std::strncmp(line, "wifi set ", 9U) == 0) {
        return handleWifiSet(&line[8], *store);
    }

    reply("ERR unknown command '%.40s'; see 'help %s'.", line, isWifi ? "wifi" : "settings");
    return CommandResult::Ok;
}

} // namespace Console
