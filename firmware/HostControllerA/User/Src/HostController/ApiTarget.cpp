#include <HostController/ApiTarget.hpp>

#include <Settings/SettingsStore.hpp>

#include "app_config.h"

#include <cstring>

namespace HostController {
namespace {

void copyBuiltIn(char* destination, std::size_t size, const char* source)
{
    std::strncpy(destination, source, size - 1U);
    destination[size - 1U] = '\0';
}

} // namespace

const char* builtInApiHost()
{
    return APP_ST67_HTTP_HOST;
}

const char* builtInApiPath()
{
    return APP_ST67_HTTP_PATH;
}

ApiTarget resolveApiTarget(const Settings::Store* store)
{
    ApiTarget target{};
    if (store != nullptr) {
        store->copyApiTarget(target.host, sizeof(target.host), target.path, sizeof(target.path));
    }
    target.hostSaved = target.host[0] != '\0';
    target.pathSaved = target.path[0] != '\0';
    if (!target.hostSaved) {
        copyBuiltIn(target.host, sizeof(target.host), builtInApiHost());
    }
    if (!target.pathSaved) {
        copyBuiltIn(target.path, sizeof(target.path), builtInApiPath());
    }
    return target;
}

} // namespace HostController
