#pragma once

#include "app_config.h"
#include <Utils/Task.hpp>

#include <cstdint>

class MainLoopTask : public Task<1536>
{
public:
    static MainLoopTask& instance();

    static void trigger();

protected:
    void run() override;

private:
    MainLoopTask();

    static constexpr uint32_t kFlagRun = 1u << 0;

    uint8_t responseBuffer_[APP_ST67_HTTP_MAX_RESPONSE_BYTES]{};
    volatile bool active_ = false;
};