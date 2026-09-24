#pragma once

#include <Settings/SettingsCodec.hpp>

namespace Settings {
class Store;
}

namespace HostController {

// The server the astro refresh fetches from. Each part is the value saved with
// 'api host' / 'api path', or, when none is saved, the built-in
// APP_ST67_HTTP_HOST / APP_ST67_HTTP_PATH from app_credentials.h.
struct ApiTarget
{
    char host[Settings::kMaxApiHostLength + 1U] = {};
    char path[Settings::kMaxApiPathLength + 1U] = {};
    bool hostSaved = false;
    bool pathSaved = false;
};

// `store` may be null, giving the built-in values. Safe from any task: the
// saved values are copied under the store's lock.
ApiTarget resolveApiTarget(const Settings::Store* store);

const char* builtInApiHost();
const char* builtInApiPath();

} // namespace HostController
