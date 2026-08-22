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

class St67ServiceTask : public Task<2560> {
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
      runConnectivityCycle();
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

  void cleanup(bool disconnect) {
    if (disconnect && wifiInitialized_) {
      state_ = State::Disconnecting;
      osThreadFlagsClear(kFlagDisconnected | kFlagDriverError);
      const W6X_Status_t status = W6X_WiFi_Disconnect(1U);
      if (status != W6X_STATUS_OK) {
        DebugService::instance().logf(DebugService::Level::Warn,
                                      "ST67 disconnect status=%d(%s)",
                                      static_cast<int>(status), W6X_StatusToStr(status));
      } else {
        (void)osThreadFlagsWait(kFlagDisconnected | kFlagDriverError,
                                osFlagsWaitAny, APP_ST67_DISCONNECT_TIMEOUT_MS);
      }
    }
    if (lwipInitialized_) {
      const int32_t status = MX_LWIP_DeInit();
      DebugService::instance().logf(
          status == 0 ? DebugService::Level::Info : DebugService::Level::Warn,
          "ST67 netif-stop status=%ld", static_cast<long>(status));
      lwipInitialized_ = false;
    }
    if (wifiInitialized_) {
      W6X_WiFi_DeInit();
      wifiInitialized_ = false;
    }
    if (w6xInitialized_) {
      W6X_DeInit();
      w6xInitialized_ = false;
    }
    state_ = State::Off;
  }

  void runConnectivityCycle() {
    if (state_ != State::Off && state_ != State::Ready && state_ != State::Complete &&
      state_ != State::Fault) {
      DebugService::instance().log(DebugService::Level::Warn,
                                   "ST67 cycle ignored: already active");
      return;
    }

    if (!credentialsValid()) {
      state_ = State::Fault;
      DebugService::instance().log(DebugService::Level::Error,
                                   "ST67 fault stage=credentials-unavailable");
      return;
    }

    state_ = (state_ == State::Off) ? State::Starting : State::Ready;
    logMemory("before");
    const uint32_t startedAt = HAL_GetTick();

    if (!w6xInitialized_) {
      lastStatus_ = W6X_Init();
      if (!logStage("w6x-init", lastStatus_, startedAt)) { fail("w6x-init"); return; }
      w6xInitialized_ = true;
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

    if (!wifiInitialized_) {
      callbacks_.APP_wifi_cb = &wifiCallback;
      callbacks_.APP_error_cb = &errorCallback;
      lastStatus_ = W6X_RegisterAppCb(&callbacks_);
      if (!logStage("callback-register", lastStatus_, startedAt)) { fail("callback-register"); cleanup(false); return; }
      lastStatus_ = W6X_WiFi_Init();
      if (!logStage("wifi-init", lastStatus_, startedAt)) { fail("wifi-init"); cleanup(false); return; }
      wifiInitialized_ = true;
    }
    logMemory("after-wifi");

    if (!lwipInitialized_) {
      if (MX_LWIP_Init() != 0) { fail("lwip-init"); cleanup(false); return; }
      lwipInitialized_ = true;
    }
    if (netif_get_interface(NETIF_STA) == nullptr ||
        netif_get_interface(NETIF_AP) == nullptr) {
      fail("lwip-netif");
      return;
    }
    logMemory("after-lwip");
    W6X_WiFi_Connect_Opts_t options{};
    std::strncpy(reinterpret_cast<char*>(options.SSID), APP_ST67_WIFI_SSID,
                 W6X_WIFI_MAX_SSID_SIZE);
    std::strncpy(reinterpret_cast<char*>(options.Password), APP_ST67_WIFI_PASSWORD,
                 W6X_WIFI_MAX_PASSWORD_SIZE);
    options.Reconnection_interval = 1U;
    options.Reconnection_nb_attempts = 1U;
    osThreadFlagsClear(kFlagConnected | kFlagDisconnected | kFlagDriverError);
    state_ = State::Connecting;
    lastStatus_ = W6X_WiFi_Connect(&options);
    if (!logStage("connect", lastStatus_, startedAt)) { fail("connect"); cleanup(true); return; }
    W6X_WiFi_StaStateType_e stationState = W6X_WIFI_STATE_STA_OFF;
    W6X_WiFi_Connect_t connection{};
    if (W6X_WiFi_Station_GetState(&stationState, &connection) != W6X_STATUS_OK ||
        stationState != W6X_WIFI_STATE_STA_CONNECTED) { fail("connect-state"); cleanup(true); return; }
    DebugService::instance().logf(DebugService::Level::Info,
                                  "ST67 connected ssid=%s channel=%lu rssi=%ld",
                                  connection.SSID, static_cast<unsigned long>(connection.Channel),
                                  static_cast<long>(connection.Rssi));
    LwipStationStatus_t network{};
    if (!waitForDhcp(&network)) { fail("dhcp"); cleanup(true); return; }
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
    cleanup(true);
    state_ = State::Complete;
    logMemory("after-cycle");
    DebugService::instance().log(DebugService::Level::Info, "ST67 connectivity cycle complete");
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