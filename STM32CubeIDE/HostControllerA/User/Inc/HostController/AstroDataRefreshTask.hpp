#pragma once

#include <HostController/AstroData.hpp>
#include <HostController/St67FetchTypes.hpp>
#include <Utils/Task.hpp>

#include "app_config.h"

#include <cstdint>

namespace Display {
class Display;
}

namespace HostController {

enum class RefreshTrigger : uint8_t
{
    Switch1,
    Console,
    Scheduled,
};

enum class RefreshRequestResult : uint8_t
{
    Accepted,
    Busy,
    Unavailable,
};

class AstroDataRefreshTask : public Task<2048>
{
public:
    static AstroDataRefreshTask& instance();

    void init(Display::Display* display);
    RefreshRequestResult requestRefresh(RefreshTrigger trigger);

protected:
    void run() override;

private:
    AstroDataRefreshTask();

    void executeRefresh(RefreshTrigger trigger);
    bool fetchPayload();
    bool publishDisplay(const AstroData& data);

    static constexpr uint32_t kFlagRun = 1U << 0;

    Display::Display* display_ = nullptr;
    uint8_t responseBuffer_[APP_ST67_HTTP_MAX_RESPONSE_BYTES]{};
    St67FetchRequest request_{};
    RefreshTrigger trigger_ = RefreshTrigger::Scheduled;
    volatile bool active_ = false;
};

const char* refreshTriggerName(RefreshTrigger trigger);

} // namespace HostController
