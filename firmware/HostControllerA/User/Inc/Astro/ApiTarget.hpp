#pragma once

#include <Settings/SettingsCodec.hpp>

namespace Settings {
class Store;
}

namespace HostController {

// The server the astro refresh fetches from. Each part is the value saved with
// 'api host' / 'api path' / 'api key', or, when none is saved, the built-in
// APP_ST67_HTTP_HOST / APP_ST67_HTTP_PATH / APP_ST67_HTTP_KEY from
// app_credentials.h. An empty key means the request carries none.
struct ApiTarget
{
    char host[Settings::kMaxApiHostLength + 1U] = {};
    char path[Settings::kMaxApiPathLength + 1U] = {};
    char key[Settings::kMaxApiKeyLength + 1U] = {};
    bool hostSaved = false;
    bool pathSaved = false;
    bool keySaved = false;
};

// `store` may be null, giving the built-in values. Safe from any task: the
// saved values are copied under the store's lock.
ApiTarget resolveApiTarget(const Settings::Store* store);

const char* builtInApiHost();
const char* builtInApiPath();
const char* builtInApiKey();

} // namespace HostController
