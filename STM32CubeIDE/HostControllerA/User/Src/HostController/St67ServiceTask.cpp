#include <St67ServiceTask.hpp>

#include <Debug/DebugService.hpp>
#include <Utils/Task.hpp>

#include "app_config.h"
#include "logging.h"
#include "lwip.h"
#include "main.h"
#include "w6x_api.h"

#include "FreeRTOS.h"
#include "task.h"

#include <cstdarg>
#include <cstdio>
#include <cstring>

extern "C" void vLoggingPrintf(uint32_t logLevel,
                                const uint8_t metadataPrint,
                                const uint32_t lineNumber,
                                const char* const fileName,
                                const char* const format,
                                ...) {
  (void)metadataPrint;
  (void)lineNumber;
  (void)fileName;

  char message[96];
  va_list arguments;
  va_start(arguments, format);
  std::vsnprintf(message, sizeof(message), format, arguments);
  va_end(arguments);

  const DebugService::Level level =
      (logLevel <= LOG_ERROR)
          ? DebugService::Level::Error
          : (logLevel == LOG_WARN) ? DebugService::Level::Warn
                                   : DebugService::Level::Info;
  DebugService::instance().log(level, message);
}

namespace {

constexpr uint32_t kFlagTrigger = 1U << 0;
constexpr uint32_t kFlagScanComplete = 1U << 1;
constexpr uint32_t kFlagDriverError = 1U << 2;
constexpr uint32_t kMaxScanResults = APP_ST67_SCAN_MAX_RESULTS;

enum class State : uint8_t {
  Off,
  Starting,
  Ready,
  Scanning,
  Complete,
  Fault,
};

class St67ServiceTask : public Task<2048> {
 public:
  static St67ServiceTask& instance() {
    static St67ServiceTask task;
    return task;
  }

  void trigger() {
    osThreadFlagsSet(getHandle(), kFlagTrigger);
  }

 protected:
  void run() override {
    osDelay(APP_ST67_STARTUP_DELAY_MS);

    for (;;) {
      const uint32_t flags = osThreadFlagsWait(kFlagTrigger, osFlagsWaitAny,
                                               osWaitForever);
      if ((flags & osFlagsError) != 0U ||
          (flags & kFlagTrigger) == 0U) {
        continue;
      }
      runSmokeTest();
    }
  }

 private:
  St67ServiceTask() : Task<2048>("St67Service", osPriorityBelowNormal) {
    std::memset(&callbacks_, 0, sizeof(callbacks_));
  }

  static void wifiCallback(W6X_event_id_t eventId, void* eventArgs) {
    (void)eventArgs;
    instance().onWifiEvent(eventId);
  }

  static void errorCallback(W6X_Status_t status, char const* functionName) {
    instance().onDriverError(status, functionName);
  }

  static void scanCallback(int32_t status, W6X_WiFi_Scan_Result_t* result) {
    instance().onScanComplete(status, result);
  }

  void onWifiEvent(W6X_event_id_t eventId) {
    lastWifiEvent_ = eventId;
  }

  void onDriverError(W6X_Status_t status, char const* functionName) {
    lastStatus_ = status;
    lastErrorFunction_ = functionName;
    osThreadFlagsSet(getHandle(), kFlagDriverError);
  }

  void onScanComplete(int32_t status, W6X_WiFi_Scan_Result_t* result) {
    scanStatus_ = status;
    scanCount_ = 0U;
    if (result != nullptr) {
      scanCount_ = (result->Count < kMaxScanResults)
                       ? result->Count
                       : kMaxScanResults;
    }
    osThreadFlagsSet(getHandle(), kFlagScanComplete);
  }

  void logMemory(const char* stage) {
    DebugService::instance().logf(
        DebugService::Level::Info,
        "ST67 %s heap=%lu min=%lu",
        stage,
        static_cast<unsigned long>(xPortGetFreeHeapSize()),
        static_cast<unsigned long>(xPortGetMinimumEverFreeHeapSize()));
  }

  bool logStage(const char* stage, W6X_Status_t status, uint32_t startedAt) {
    DebugService::instance().logf(
        (status == W6X_STATUS_OK) ? DebugService::Level::Info
                                  : DebugService::Level::Error,
        "ST67 %s status=%d(%s) elapsed=%lums",
        stage,
        static_cast<int>(status),
        W6X_StatusToStr(status),
        static_cast<unsigned long>(HAL_GetTick() - startedAt));
    return status == W6X_STATUS_OK;
  }

  void fail(const char* stage) {
    state_ = State::Fault;
    DebugService::instance().logf(DebugService::Level::Error,
                                  "ST67 fault stage=%s status=%d source=%s",
                                  stage,
                                  static_cast<int>(lastStatus_),
                                  (lastErrorFunction_ != nullptr)
                                      ? lastErrorFunction_
                                      : "application");
  }

  void runSmokeTest() {
    if (state_ != State::Off) {
      DebugService::instance().log(DebugService::Level::Warn,
                                   "ST67 smoke test ignored: already started");
      return;
    }

    state_ = State::Starting;
    logMemory("before");
    const uint32_t startedAt = HAL_GetTick();

    lastStatus_ = W6X_Init();
    if (!logStage("w6x-init", lastStatus_, startedAt)) {
      fail("w6x-init");
      return;
    }
    logMemory("after-w6x");

    W6X_ModuleInfo_t* moduleInfo = W6X_GetModuleInfo();
    if (moduleInfo == nullptr) {
      fail("module-info");
      return;
    }
    DebugService::instance().logf(
        DebugService::Level::Info,
        "ST67 module=%s sdk=%u.%u.%u.%u",
        moduleInfo->ModuleID.ModuleName,
        moduleInfo->SDK_Version.Major,
        moduleInfo->SDK_Version.Sub1,
        moduleInfo->SDK_Version.Sub2,
        moduleInfo->SDK_Version.Patch);

    callbacks_.APP_wifi_cb = &wifiCallback;
    callbacks_.APP_error_cb = &errorCallback;
    lastStatus_ = W6X_RegisterAppCb(&callbacks_);
    if (!logStage("callback-register", lastStatus_, startedAt)) {
      fail("callback-register");
      W6X_DeInit();
      return;
    }

    lastStatus_ = W6X_WiFi_Init();
    if (!logStage("wifi-init", lastStatus_, startedAt)) {
      fail("wifi-init");
      W6X_DeInit();
      return;
    }
    logMemory("after-wifi");

    if (MX_LWIP_Init() != 0) {
      fail("lwip-init");
      return;
    }
    if (netif_get_interface(NETIF_STA) == nullptr ||
        netif_get_interface(NETIF_AP) == nullptr) {
      fail("lwip-netif");
      return;
    }
    logMemory("after-lwip");
    state_ = State::Ready;

    W6X_WiFi_Scan_Opts_t options{};
    options.Scan_type = W6X_WIFI_SCAN_ACTIVE;
    options.MaxCnt = static_cast<uint8_t>(APP_ST67_SCAN_MAX_RESULTS);
    scanStatus_ = -1;
    scanCount_ = 0U;
    osThreadFlagsClear(kFlagScanComplete | kFlagDriverError);
    state_ = State::Scanning;

    lastStatus_ = W6X_WiFi_Scan(&options, &scanCallback);
    if (!logStage("scan-start", lastStatus_, startedAt)) {
      fail("scan-start");
      return;
    }

    const uint32_t waitResult = osThreadFlagsWait(
        kFlagScanComplete | kFlagDriverError, osFlagsWaitAny,
        APP_ST67_SCAN_TIMEOUT_MS);
    if ((waitResult & osFlagsError) != 0U ||
        (waitResult & (kFlagScanComplete | kFlagDriverError)) == 0U) {
      fail("scan-wait");
      return;
    }
    if ((waitResult & kFlagDriverError) != 0U) {
      fail("driver-callback");
      return;
    }

    lastStatus_ = static_cast<W6X_Status_t>(scanStatus_);
    DebugService::instance().logf(
        (lastStatus_ == W6X_STATUS_OK) ? DebugService::Level::Info
                                       : DebugService::Level::Error,
        "ST67 scan status=%ld aps=%lu elapsed=%lums",
        static_cast<long>(scanStatus_),
        static_cast<unsigned long>(scanCount_),
        static_cast<unsigned long>(HAL_GetTick() - startedAt));
    logMemory("after-scan");
    state_ = (lastStatus_ == W6X_STATUS_OK) ? State::Complete : State::Fault;
    if (state_ == State::Fault) {
      fail("driver-callback");
    } else {
      DebugService::instance().log(DebugService::Level::Info,
                                   "ST67 smoke test complete");
    }
  }

  State state_ = State::Off;
  W6X_App_Cb_t callbacks_{};
  W6X_Status_t lastStatus_ = W6X_STATUS_OK;
  const char* lastErrorFunction_ = nullptr;
  W6X_event_id_t lastWifiEvent_ = 0U;
  int32_t scanStatus_ = -1;
  uint32_t scanCount_ = 0U;
};

}  // namespace

namespace HostController {

void StartSt67ServiceTask() {
  St67ServiceTask::instance().start();
}

void TriggerSt67SmokeTest() {
  St67ServiceTask::instance().trigger();
}

}  // namespace HostController