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
    WifiTest,  // 'wifi set' / 'wifi test': a refresh used to exercise the credentials
};

enum class RefreshRequestResult : uint8_t
{
    Accepted,
    Busy,
    Unavailable,
};

// How the most recent refresh ended, for status reporting. The log's
// "AstroDataRefresh complete" line is printed whatever the outcome, so it is not
// evidence of success on its own.
enum class RefreshOutcome : uint8_t
{
    Never,           // no refresh has run since boot
    Ok,              // fetched, verified, parsed and published
    FetchFailed,     // ST67/network/HTTP failure; see fetchStatus and httpStatus
    IntegrityFailed, // payload CRC did not match
    ParseFailed,
    PublishFailed,
};

struct RefreshSummary
{
    RefreshOutcome outcome = RefreshOutcome::Never;
    RefreshTrigger trigger = RefreshTrigger::Scheduled;
    St67FetchStatus fetchStatus = St67FetchStatus::Success;
    uint16_t httpStatus = 0U;
    uint32_t finishedTick = 0U;  // osKernelGetTickCount() when it finished
    bool running = false;
};

// 3 KB: the parsed forecast (~700 B), the clock sync and their formatted log
// lines all sit on this stack. 2 KB overflowed once the sync was added.
class AstroDataRefreshTask : public Task<3072>
{
public:
    static AstroDataRefreshTask& instance();

    void init(Display::Display* display);
    RefreshRequestResult requestRefresh(RefreshTrigger trigger);
    // Safe to call from any task.
    RefreshSummary lastRefresh() const;

protected:
    void run() override;

private:
    AstroDataRefreshTask();

    void executeRefresh(RefreshTrigger trigger);
    bool fetchPayload();
    bool publishDisplay(const AstroData& data);

    // Progress bar on the local board's bottom matrix row. Drawn from this task
    // only, while it waits for the WiFi task, so the WiFi task never touches the
    // display.
    enum class Indicator : uint8_t { None, Success, Failure };
    static void onFetchProgress(FetchStage stage, void* context);
    void showProgressRow(uint32_t columns);
    void startIndicator(Indicator indicator, uint8_t failedSegment);
    void stepIndicator();
    void clearIndicator();

    static constexpr uint32_t kFlagRun = 1U << 0;

    Display::Display* display_ = nullptr;
    uint8_t responseBuffer_[APP_ST67_HTTP_MAX_RESPONSE_BYTES]{};
    St67FetchRequest request_{};
    RefreshTrigger trigger_ = RefreshTrigger::Scheduled;
    volatile bool active_ = false;
    RefreshSummary last_{};
    Indicator indicator_ = Indicator::None;
    uint32_t indicatorUntil_ = 0U;
    uint8_t failedSegment_ = 0U;
    bool indicatorLit_ = false;
    uint32_t shownRow_ = 0xFFFFFFFFU;  // columns last submitted; none yet
    uint8_t loggedStage_ = 0xFFU;
};

const char* refreshTriggerName(RefreshTrigger trigger);
const char* refreshOutcomeName(RefreshOutcome outcome);
const char* fetchStatusName(St67FetchStatus status);

} // namespace HostController
