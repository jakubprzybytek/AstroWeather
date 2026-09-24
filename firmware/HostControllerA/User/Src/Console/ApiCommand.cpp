#include <Console/ApiCommand.hpp>

#include <Debug/LogService.hpp>
#include <HostController/ApiTarget.hpp>
#include <HostController/St67HttpRules.hpp>
#include <Settings/SettingsStore.hpp>

#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace Console {
namespace {

void reply(const char* format, ...)
{
    char text[160];
    va_list arguments;
    va_start(arguments, format);
    std::vsnprintf(text, sizeof(text), format, arguments);
    va_end(arguments);
    LogService::instance().sendLine(text);
}

void show(const Settings::Store* store)
{
    const HostController::ApiTarget target = HostController::resolveApiTarget(store);
    reply("OK api host=%s (%s)", target.host, target.hostSaved ? "saved" : "built-in");
    reply("OK api path=%s (%s)", target.path, target.pathSaved ? "saved" : "built-in");
}

bool save(Settings::Store& store)
{
    const HAL_StatusTypeDef status = store.save();
    if (status != HAL_OK) {
        LogService::instance().logf(LogService::Level::Error, "Settings save failed status=%u",
                                    static_cast<unsigned>(status));
        reply("ERR settings-unavailable");
        return false;
    }
    return true;
}

// The same rules the fetch applies, checked here so a bad value is refused
// with a reason instead of failing at the next refresh.
void setHost(const char* host, Settings::Store& store)
{
    if (std::strncmp(host, "http://", 7U) == 0 || std::strncmp(host, "https://", 8U) == 0) {
        reply("ERR api-host: give the bare host name, without http:// (HTTP only, port 80), "
              "e.g. api host api.example.com");
        return;
    }
    if (std::strlen(host) > Settings::kMaxApiHostLength ||
        !HostController::St67HttpRules::isValidTarget(host, "/", Settings::kMaxApiHostLength)) {
        reply("ERR api-host: must be 1-%u characters with no port, path or spaces, "
              "e.g. api host api.example.com",
              static_cast<unsigned>(Settings::kMaxApiHostLength));
        return;
    }
    store.setApiHost(host);
    if (save(store)) {
        reply("OK api-host=%s", host);
    }
}

void setPath(const char* path, Settings::Store& store)
{
    if (std::strlen(path) > Settings::kMaxApiPathLength ||
        !HostController::St67HttpRules::isValidTarget("host", path, Settings::kMaxApiHostLength)) {
        reply("ERR api-path: must start with / and be 1-%u characters with no spaces, "
              "e.g. api path /astro/wroclaw",
              static_cast<unsigned>(Settings::kMaxApiPathLength));
        return;
    }
    store.setApiPath(path);
    if (save(store)) {
        reply("OK api-path=%s", path);
    }
}

} // namespace

CommandResult handleApiCommand(const char* line, Settings::Store* store)
{
    if (std::strcmp(line, "api") != 0 && std::strncmp(line, "api ", 4U) != 0) {
        return CommandResult::NotHandled;
    }

    if (std::strcmp(line, "api") == 0 || std::strcmp(line, "api show") == 0) {
        show(store);
        return CommandResult::Ok;
    }
    if (store == nullptr) {
        reply("ERR settings-unavailable");
        return CommandResult::Ok;
    }
    if (std::strcmp(line, "api host") == 0 || std::strcmp(line, "api path") == 0) {
        reply("ERR api-%s: missing value. Usage: api host <host>, api path <path>", &line[4]);
        return CommandResult::Ok;
    }
    if (std::strncmp(line, "api host ", 9U) == 0) {
        setHost(&line[9], *store);
        return CommandResult::Ok;
    }
    if (std::strncmp(line, "api path ", 9U) == 0) {
        setPath(&line[9], *store);
        return CommandResult::Ok;
    }
    if (std::strcmp(line, "api default") == 0) {
        store->setApiHost("");
        store->setApiPath("");
        if (save(*store)) {
            reply("OK api-default");
            show(store);
        }
        return CommandResult::Ok;
    }
    reply("ERR unknown command '%.40s'; see 'help api'.", line);
    return CommandResult::Ok;
}

} // namespace Console
