#include <St67ServiceTask.hpp>

#include <Debug/DebugService.hpp>
#include <Utils/Task.hpp>

#include "app_config.h"
#include "logging.h"
#include "lwip.h"
#include "lwip_netif.h"
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
constexpr uint32_t kFlagConnected = 1U << 1;
constexpr uint32_t kFlagDisconnected = 1U << 2;
constexpr uint32_t kFlagDriverError = 1U << 3;

enum class State : uint8_t {
  Off,
  Starting,
  Ready,
  Connecting,
  Online,
  Disconnecting,
  Complete,
  Fault,
};

static_assert(APP_ST67_LIFECYCLE_MODE == APP_ST67_LIFECYCLE_SINGLE_FULL_SHUTDOWN ||
                  APP_ST67_LIFECYCLE_MODE == APP_ST67_LIFECYCLE_PERSISTENT_STRESS ||
                  APP_ST67_LIFECYCLE_MODE == APP_ST67_LIFECYCLE_COLD_RESTART_STRESS,
              "Unknown ST67 lifecycle mode");
static_assert(APP_ST67_PERSISTENT_STRESS_CYCLES > 0U,
              "Persistent stress requires at least one cycle");
static_assert(APP_ST67_COLD_RESTART_STRESS_CYCLES > 0U,
              "Cold restart stress requires at least one cycle");

enum class LifecycleMode : uint8_t {
  SingleFullShutdown = APP_ST67_LIFECYCLE_SINGLE_FULL_SHUTDOWN,
  PersistentStress = APP_ST67_LIFECYCLE_PERSISTENT_STRESS,
  ColdRestartStress = APP_ST67_LIFECYCLE_COLD_RESTART_STRESS,
};

struct BatchResult {
  LifecycleMode mode;
  uint32_t requestedCycles = 0U;
  uint32_t attemptedCycles = 0U;
  uint32_t passedCycles = 0U;
  uint32_t failedCycles = 0U;
  uint32_t firstFailedCycle = 0U;
  const char* firstFailureStage = nullptr;
  W6X_Status_t firstFailureStatus = W6X_STATUS_OK;
  uint32_t startingHeap = 0U;
  uint32_t endingHeap = 0U;
  uint32_t lowestHeap = UINT32_MAX;
  UBaseType_t startingTasks = 0U;
  UBaseType_t endingTasks = 0U;
};

class St67ServiceTask : public Task<2560> {
 public:
  static St67ServiceTask& instance() {
    static St67ServiceTask task;
    return task;
  }

  void trigger() {
    if (batchActive_) {
      triggerRejected_ = true;
    }
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
      runBatch();
    }
  }

 private:
  St67ServiceTask() : Task<2560>("St67Service", osPriorityBelowNormal) {
    std::memset(&callbacks_, 0, sizeof(callbacks_));
  }

  static void wifiCallback(W6X_event_id_t eventId, void* eventArgs) {
    instance().onWifiEvent(eventId, eventArgs);
  }

  static void errorCallback(W6X_Status_t status, char const* functionName) {
    instance().onDriverError(status, functionName);
  }

  static void scanCallback(int32_t status, W6X_WiFi_Scan_Result_t* result) {
    (void)status;
    (void)result;
  }

  void onWifiEvent(W6X_event_id_t eventId, void* eventArgs) {
    lastWifiEvent_ = eventId;
    if (eventId == W6X_WIFI_EVT_REASON_ID) {
      lastWifiReason_ = (eventArgs != nullptr) ? *static_cast<uint32_t*>(eventArgs) : 0U;
    }
    if (eventId == W6X_WIFI_EVT_CONNECTED_ID) {
      osThreadFlagsSet(getHandle(), kFlagConnected);
    } else if (eventId == W6X_WIFI_EVT_DISCONNECTED_ID) {
      osThreadFlagsSet(getHandle(), kFlagDisconnected);
    }
  }

  void onDriverError(W6X_Status_t status, char const* functionName) {
    lastStatus_ = status;
    lastErrorFunction_ = functionName;
    osThreadFlagsSet(getHandle(), kFlagDriverError);
  }

  void logMemory(const char* stage) {
    DebugService::instance().logf(
        DebugService::Level::Info,
        "ST67 %s heap=%lu min=%lu tasks=%lu",
        stage,
        static_cast<unsigned long>(xPortGetFreeHeapSize()),
        static_cast<unsigned long>(xPortGetMinimumEverFreeHeapSize()),
        static_cast<unsigned long>(uxTaskGetNumberOfTasks()));
  }

        void logCheckpoint(const char* stage) {
          DebugService::instance().logf(
          DebugService::Level::Info,
          "ST67 checkpoint=%s heap=%lu min=%lu tasks=%lu pbufs=%lu running=%u sta=%u ap=%u chip=%u rdy=%u",
          stage,
          static_cast<unsigned long>(xPortGetFreeHeapSize()),
          static_cast<unsigned long>(xPortGetMinimumEverFreeHeapSize()),
          static_cast<unsigned long>(uxTaskGetNumberOfTasks()),
          static_cast<unsigned long>(net_if_outstanding_pbufs()),
          static_cast<unsigned int>(net_if_is_running()),
          netif_get_interface(NETIF_STA) != nullptr ? 1U : 0U,
          netif_get_interface(NETIF_AP) != nullptr ? 1U : 0U,
          HAL_GPIO_ReadPin(ST67_CHIP_EN_GPIO_Port, ST67_CHIP_EN_Pin) == GPIO_PIN_SET ? 1U : 0U,
          HAL_GPIO_ReadPin(ST67_RDY_GPIO_Port, ST67_RDY_Pin) == GPIO_PIN_SET ? 1U : 0U);
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
    if (firstFailureStage_ == nullptr) {
      firstFailureStage_ = stage;
      firstFailureStatus_ = lastStatus_;
    }
    state_ = State::Fault;
  }

  bool stationDisconnected() const {
    W6X_WiFi_StaStateType_e stationState = W6X_WIFI_STATE_STA_OFF;
    if (W6X_WiFi_Station_GetState(&stationState, nullptr) != W6X_STATUS_OK ||
        (stationState != W6X_WIFI_STATE_STA_DISCONNECTED &&
         stationState != W6X_WIFI_STATE_STA_OFF)) {
      return false;
    }
    LwipStationStatus_t network{};
    return lwip_get_station_status(&network) == 0 && network.link_up == 0U &&
           network.has_ipv4 == 0U;
  }

  bool finalHardwareState() const {
    return lwip_netifs_are_removed() != 0U && net_if_is_running() == 0U &&
           HAL_GPIO_ReadPin(ST67_CHIP_EN_GPIO_Port, ST67_CHIP_EN_Pin) == GPIO_PIN_RESET &&
           HAL_GPIO_ReadPin(ST67_RDY_GPIO_Port, ST67_RDY_Pin) == GPIO_PIN_RESET;
  }

  void logFinalResult() {
    DebugService::instance().logf(
        firstFailureStage_ == nullptr ? DebugService::Level::Info
                                      : DebugService::Level::Error,
        "ST67 cycle=%lu result=%s stage=%s status=%d heap=%lu min=%lu tasks=%lu",
        static_cast<unsigned long>(cycleId_),
        firstFailureStage_ == nullptr ? "complete" : "fault",
        firstFailureStage_ == nullptr ? "none" : firstFailureStage_,
        static_cast<int>(firstFailureStage_ == nullptr ? lastStatus_ : firstFailureStatus_),
        static_cast<unsigned long>(xPortGetFreeHeapSize()),
        static_cast<unsigned long>(xPortGetMinimumEverFreeHeapSize()),
        static_cast<unsigned long>(uxTaskGetNumberOfTasks()));
  }

  bool credentialsValid() const {
    const size_t ssidLength = std::strlen(APP_ST67_WIFI_SSID);
    const size_t passwordLength = std::strlen(APP_ST67_WIFI_PASSWORD);
    return ssidLength != 0U && ssidLength <= W6X_WIFI_MAX_SSID_SIZE &&
           passwordLength <= W6X_WIFI_MAX_PASSWORD_SIZE;
  }

  bool waitForDhcp(LwipStationStatus_t* status) {
    const uint32_t deadline = HAL_GetTick() + APP_ST67_DHCP_TIMEOUT_MS;
    do {
      if (lwip_get_station_status(status) == 0 && status->link_up != 0U &&
          status->interface_up != 0U && status->has_ipv4 != 0U) {
        return true;
      }
      osDelay(100U);
    } while (static_cast<int32_t>(HAL_GetTick() - deadline) < 0);
    return false;
  }

  bool disconnectStation() {
    logCheckpoint("before-disconnect");
    if (!wifiInitialized_) {
      return true;
    }

    state_ = State::Disconnecting;
    osThreadFlagsClear(kFlagDisconnected | kFlagDriverError);
    const W6X_Status_t status = W6X_WiFi_Disconnect(1U);
    if (status != W6X_STATUS_OK) {
      lastStatus_ = status;
      fail("disconnect");
      return false;
    }
    const uint32_t flags = osThreadFlagsWait(
        kFlagDisconnected | kFlagDriverError, osFlagsWaitAny,
        APP_ST67_DISCONNECT_TIMEOUT_MS);
    if ((flags & kFlagDisconnected) == 0U) {
      fail("disconnect");
      return false;
    }
    if (!stationDisconnected()) {
      fail("link-down");
      return false;
    }
    osDelay(100U);
    if (!stationDisconnected()) {
      fail("reconnect");
      return false;
    }
    state_ = State::Ready;
    logCheckpoint("after-disconnect");

    return true;
  }

  bool initializeStack(bool logModule) {
    if (!credentialsValid()) {
      fail("credentials-unavailable");
      return false;
    }
    state_ = State::Starting;
    const uint32_t startedAt = HAL_GetTick();
    if (!w6xInitialized_) {
      lastStatus_ = W6X_Init();
      if (!logStage("w6x-init", lastStatus_, startedAt)) {
        fail("w6x-init");
        return false;
      }
      w6xInitialized_ = true;
    }
    logMemory("after-w6x");

    if (logModule) {
      W6X_ModuleInfo_t* moduleInfo = W6X_GetModuleInfo();
      if (moduleInfo == nullptr) {
        fail("module-info");
        return false;
      }
      DebugService::instance().logf(
          DebugService::Level::Info,
          "ST67 module=%s sdk=%u.%u.%u.%u",
          moduleInfo->ModuleID.ModuleName,
          moduleInfo->SDK_Version.Major,
          moduleInfo->SDK_Version.Sub1,
          moduleInfo->SDK_Version.Sub2,
          moduleInfo->SDK_Version.Patch);
    }

    if (!wifiInitialized_) {
      callbacks_.APP_wifi_cb = &wifiCallback;
      callbacks_.APP_error_cb = &errorCallback;
      lastStatus_ = W6X_RegisterAppCb(&callbacks_);
      if (!logStage("callback-register", lastStatus_, startedAt)) {
        fail("callback-register");
        return false;
      }
      lastStatus_ = W6X_WiFi_Init();
      if (!logStage("wifi-init", lastStatus_, startedAt)) {
        fail("wifi-init");
        return false;
      }
      wifiInitialized_ = true;
    }
    logMemory("after-wifi");

    if (!lwipInitialized_) {
      if (MX_LWIP_Init() != 0) {
        fail("lwip-init");
        return false;
      }
      lwipInitialized_ = true;
    }
    if (netif_get_interface(NETIF_STA) == nullptr ||
        netif_get_interface(NETIF_AP) == nullptr || !net_if_is_running()) {
      fail("lwip-netif");
      return false;
    }
    state_ = State::Ready;
    logMemory("after-lwip");
    return true;
  }

  bool runStationIteration() {
    firstFailureStage_ = nullptr;
    firstFailureStatus_ = W6X_STATUS_OK;
    osThreadFlagsClear(kFlagConnected | kFlagDisconnected | kFlagDriverError);
    W6X_WiFi_Connect_Opts_t options{};
    std::strncpy(reinterpret_cast<char*>(options.SSID), APP_ST67_WIFI_SSID,
                 W6X_WIFI_MAX_SSID_SIZE);
    std::strncpy(reinterpret_cast<char*>(options.Password), APP_ST67_WIFI_PASSWORD,
                 W6X_WIFI_MAX_PASSWORD_SIZE);
    options.Reconnection_interval = 1U;
    options.Reconnection_nb_attempts = 1U;
    const uint32_t startedAt = HAL_GetTick();
    state_ = State::Connecting;
    lastStatus_ = W6X_WiFi_Connect(&options);
    if (!logStage("connect", lastStatus_, startedAt)) {
      fail("connect");
      disconnectStation();
      return false;
    }
    W6X_WiFi_StaStateType_e stationState = W6X_WIFI_STATE_STA_OFF;
    W6X_WiFi_Connect_t connection{};
    if (W6X_WiFi_Station_GetState(&stationState, &connection) != W6X_STATUS_OK ||
        stationState != W6X_WIFI_STATE_STA_CONNECTED) {
      fail("connect-state");
      disconnectStation();
      return false;
    }
    DebugService::instance().logf(DebugService::Level::Info,
                                  "ST67 connected ssid=%s channel=%lu rssi=%ld",
                                  connection.SSID, static_cast<unsigned long>(connection.Channel),
                                  static_cast<long>(connection.Rssi));
    LwipStationStatus_t network{};
    if (!waitForDhcp(&network)) {
      fail("dhcp");
      disconnectStation();
      return false;
    }
    state_ = State::Online;
    char addressText[16];
    char netmaskText[16];
    char gatewayText[16];
    char dnsText[40];
    ip4addr_ntoa_r(&network.address, addressText, sizeof(addressText));
    ip4addr_ntoa_r(&network.netmask, netmaskText, sizeof(netmaskText));
    ip4addr_ntoa_r(&network.gateway, gatewayText, sizeof(gatewayText));
    ipaddr_ntoa_r(&network.dns_server, dnsText, sizeof(dnsText));
    DebugService::instance().logf(DebugService::Level::Info,
                    "ST67 online reason=%lu ip=%s mask=%s gateway=%s dns=%s",
                    static_cast<unsigned long>(lastWifiReason_),
                    addressText, netmaskText, gatewayText, dnsText);
    return disconnectStation();
  }

  bool persistentReady() const {
    return state_ == State::Ready && w6xInitialized_ && wifiInitialized_ &&
           lwipInitialized_ && stationDisconnected() &&
           netif_get_interface(NETIF_STA) != nullptr &&
           netif_get_interface(NETIF_AP) != nullptr && net_if_is_running() != 0U &&
           net_if_outstanding_pbufs() == 0U;
  }

  bool shutdownStack() {
    if (wifiInitialized_ && !stationDisconnected()) {
      disconnectStation();
    }
    logCheckpoint("after-disconnect");
    if (lwipInitialized_) {
      const int32_t status = MX_LWIP_DeInit();
      if (status != 0) {
        fail("netif-stop");
      }
      DebugService::instance().logf(
          status == 0 ? DebugService::Level::Info : DebugService::Level::Warn,
          "ST67 netif-stop status=%ld pbufs=%lu running=%u",
          static_cast<long>(status),
          static_cast<unsigned long>(net_if_outstanding_pbufs()),
          static_cast<unsigned int>(net_if_is_running()));
      lwipInitialized_ = false;
    }
    logCheckpoint("after-lwip");
    if (wifiInitialized_) {
      logCheckpoint("before-wifi-deinit");
      W6X_WiFi_DeInit();
      wifiInitialized_ = false;
      logCheckpoint("after-wifi-deinit");
    }
    logCheckpoint("after-wifi");
    if (w6xInitialized_) {
      logCheckpoint("before-w6x-deinit");
      W6X_DeInit();
      w6xInitialized_ = false;
      logCheckpoint("after-w6x-deinit");
    }
    logCheckpoint("after-w6x");
    osDelay(APP_ST67_SHUTDOWN_SETTLING_DELAY_MS);
    if (!finalHardwareState()) {
      fail("final-state");
    }
    logCheckpoint("after-stabilize");
    state_ = State::Off;
    return firstFailureStage_ == nullptr;
  }

  void runBatch() {
    if (batchActive_) {
      return;
    }
    batchActive_ = true;
    osThreadFlagsClear(kFlagTrigger);
    const LifecycleMode mode = static_cast<LifecycleMode>(APP_ST67_LIFECYCLE_MODE);
    const uint32_t requestedCycles =
        mode == LifecycleMode::SingleFullShutdown
            ? 1U
            : (mode == LifecycleMode::PersistentStress
                   ? APP_ST67_PERSISTENT_STRESS_CYCLES
                   : APP_ST67_COLD_RESTART_STRESS_CYCLES);
    BatchResult result{mode, requestedCycles};
    result.startingHeap = xPortGetFreeHeapSize();
    result.lowestHeap = xPortGetMinimumEverFreeHeapSize();
    result.startingTasks = uxTaskGetNumberOfTasks();
    DebugService::instance().logf(DebugService::Level::Info,
                                  "ST67 batch-start mode=%u cycles=%lu heap=%lu tasks=%lu",
                                  static_cast<unsigned int>(mode),
                                  static_cast<unsigned long>(requestedCycles),
                                  static_cast<unsigned long>(result.startingHeap),
                                  static_cast<unsigned long>(result.startingTasks));

    bool initialized = false;
    for (uint32_t cycle = 0U; cycle < requestedCycles; ++cycle) {
      ++result.attemptedCycles;
      ++cycleId_;
      firstFailureStage_ = nullptr;
      firstFailureStatus_ = W6X_STATUS_OK;
      if (mode == LifecycleMode::ColdRestartStress || !initialized) {
        if (!initializeStack(result.attemptedCycles == 1U)) {
          shutdownStack();
        } else {
          initialized = true;
        }
      }
      const bool readyForIteration =
          initialized && (mode != LifecycleMode::PersistentStress || cycle == 0U ||
                          persistentReady());
      if (!readyForIteration) {
        fail("persistent-ready");
      }
      const bool iterationPassed = readyForIteration && runStationIteration();
      const bool teardownPassed =
          mode == LifecycleMode::PersistentStress ? true : shutdownStack();
      if (mode != LifecycleMode::PersistentStress) {
        initialized = false;
      }
      if (iterationPassed && teardownPassed && firstFailureStage_ == nullptr) {
        ++result.passedCycles;
      } else {
        ++result.failedCycles;
        if (result.firstFailedCycle == 0U) {
          result.firstFailedCycle = cycleId_;
          result.firstFailureStage = firstFailureStage_;
          result.firstFailureStatus = firstFailureStatus_;
        }
      }
      const uint32_t currentHeap = xPortGetFreeHeapSize();
      result.lowestHeap = currentHeap < result.lowestHeap ? currentHeap : result.lowestHeap;
      const uint32_t minimumEverHeap = xPortGetMinimumEverFreeHeapSize();
      result.lowestHeap = minimumEverHeap < result.lowestHeap
                              ? minimumEverHeap
                              : result.lowestHeap;
      logFinalResult();
      if (!iterationPassed || !teardownPassed) {
        break;
      }
      if (mode == LifecycleMode::PersistentStress && cycle + 1U < requestedCycles) {
        osDelay(APP_ST67_INTER_CYCLE_DELAY_MS);
      } else if (mode == LifecycleMode::ColdRestartStress && cycle + 1U < requestedCycles) {
        osDelay(APP_ST67_COLD_RESTART_DELAY_MS);
      }
    }
    if (mode == LifecycleMode::PersistentStress) {
      const bool teardownPassed = shutdownStack();
      if (!teardownPassed && result.firstFailedCycle == 0U) {
        result.firstFailedCycle = cycleId_;
        result.firstFailureStage = firstFailureStage_;
        result.firstFailureStatus = firstFailureStatus_;
        ++result.failedCycles;
      }
    }
    result.endingHeap = xPortGetFreeHeapSize();
    result.endingTasks = uxTaskGetNumberOfTasks();
    DebugService::instance().logf(
        result.failedCycles == 0U ? DebugService::Level::Info : DebugService::Level::Error,
        "ST67 batch-final mode=%u pass=%lu fail=%lu first=%lu stage=%s status=%d heap=%lu/%lu min=%lu tasks=%lu/%lu",
        static_cast<unsigned int>(mode),
        static_cast<unsigned long>(result.passedCycles),
        static_cast<unsigned long>(result.failedCycles),
        static_cast<unsigned long>(result.firstFailedCycle),
        result.firstFailureStage == nullptr ? "none" : result.firstFailureStage,
        static_cast<int>(result.firstFailureStatus),
        static_cast<unsigned long>(result.startingHeap),
        static_cast<unsigned long>(result.endingHeap),
        static_cast<unsigned long>(result.lowestHeap),
        static_cast<unsigned long>(result.startingTasks),
        static_cast<unsigned long>(result.endingTasks));
    if (triggerRejected_) {
      DebugService::instance().log(DebugService::Level::Warn,
                                   "ST67 batch trigger rejected: active");
      triggerRejected_ = false;
    }
    osThreadFlagsClear(kFlagTrigger);
    batchActive_ = false;
  }

  State state_ = State::Off;
  W6X_App_Cb_t callbacks_{};
  W6X_Status_t lastStatus_ = W6X_STATUS_OK;
  const char* lastErrorFunction_ = nullptr;
  uint32_t lastWifiReason_ = 0U;
  W6X_event_id_t lastWifiEvent_ = 0U;
  bool w6xInitialized_ = false;
  bool wifiInitialized_ = false;
  bool lwipInitialized_ = false;
  uint32_t cycleId_ = 0U;
  bool batchActive_ = false;
  bool triggerRejected_ = false;
  const char* firstFailureStage_ = nullptr;
  W6X_Status_t firstFailureStatus_ = W6X_STATUS_OK;
};

}  // namespace

namespace HostController {

void StartSt67ServiceTask() {
  St67ServiceTask::instance().start();
}

void TriggerSt67SmokeTest() {
  TriggerSt67ConnectivityCycle();
}

void TriggerSt67ConnectivityCycle() {
  St67ServiceTask::instance().trigger();
}

}  // namespace HostController