#include <HostController/St67NetworkSession.hpp>

#include <Debug/DebugService.hpp>
#include <HostController/St67NetworkAdapter.hpp>
#include <HostController/St67Runtime.hpp>

#include "app_config.h"
#include "lwip.h"
#include "lwip_netif.h"
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"

#include <cstring>

namespace HostController {
namespace {

constexpr uint32_t kFlagConnected = 1U << 1;
constexpr uint32_t kFlagDisconnected = 1U << 2;
constexpr uint32_t kFlagDriverError = 1U << 3;

St67Runtime* activeRuntime = nullptr;

void fail(St67Runtime& runtime, const char* stage) {
  if (runtime.firstFailureStage == nullptr) {
    runtime.firstFailureStage = stage;
    runtime.firstFailureStatus = runtime.lastStatus;
  }
  runtime.state = St67State::Fault;
}

void wifiCallback(W6X_event_id_t eventId, void* eventArgs) {
  if (activeRuntime == nullptr) {
    return;
  }
  activeRuntime->lastWifiEvent = eventId;
  if (eventId == W6X_WIFI_EVT_REASON_ID && eventArgs != nullptr) {
    activeRuntime->lastWifiReason = *static_cast<uint32_t*>(eventArgs);
  }
  if (activeRuntime->taskHandle == nullptr) {
    return;
  }
  if (eventId == W6X_WIFI_EVT_CONNECTED_ID) {
    osThreadFlagsSet(activeRuntime->taskHandle, kFlagConnected);
  } else if (eventId == W6X_WIFI_EVT_DISCONNECTED_ID) {
    osThreadFlagsSet(activeRuntime->taskHandle, kFlagDisconnected);
  }
}

void errorCallback(W6X_Status_t status, char const* functionName) {
  if (activeRuntime == nullptr) {
    return;
  }
  activeRuntime->lastStatus = status;
  activeRuntime->lastErrorFunction = functionName;
  if (activeRuntime->taskHandle != nullptr) {
    osThreadFlagsSet(activeRuntime->taskHandle, kFlagDriverError);
  }
}

bool logStage(St67Runtime& runtime, const char* stage, W6X_Status_t status,
              uint32_t startedAt) {
  DebugService::instance().logf(
      status == W6X_STATUS_OK ? DebugService::Level::Debug
                              : DebugService::Level::Error,
      "ST67 %s status=%d(%s) elapsed=%lums", stage, static_cast<int>(status),
      W6X_StatusToStr(status),
      static_cast<unsigned long>(HAL_GetTick() - startedAt));
  return status == W6X_STATUS_OK;
}

bool credentialsValid() {
  const size_t ssidLength = std::strlen(APP_ST67_WIFI_SSID);
  const size_t passwordLength = std::strlen(APP_ST67_WIFI_PASSWORD);
  return ssidLength != 0U && ssidLength <= W6X_WIFI_MAX_SSID_SIZE &&
         passwordLength <= W6X_WIFI_MAX_PASSWORD_SIZE;
}

bool stationDisconnected(const St67Runtime& runtime) {
  (void)runtime;
  St67StationStatus status{};
  if (!St67GetStationStatus(&status)) {
    return false;
  }
  return status.wifiDisconnected && !status.linkUp && !status.hasIpv4;
}

bool finalHardwareState() {
  return HAL_GPIO_ReadPin(ST67_CHIP_EN_GPIO_Port, ST67_CHIP_EN_Pin) == GPIO_PIN_RESET &&
         HAL_GPIO_ReadPin(ST67_RDY_GPIO_Port, ST67_RDY_Pin) == GPIO_PIN_RESET;
}

bool waitForDhcp() {
  const uint32_t deadline = HAL_GetTick() + APP_ST67_DHCP_TIMEOUT_MS;
  do {
    St67StationStatus status{};
    if (St67GetStationStatus(&status) && status.interfaceUp && status.linkUp &&
        status.hasIpv4) {
      return true;
    }
    osDelay(100U);
  } while (static_cast<int32_t>(HAL_GetTick() - deadline) < 0);
  return false;
}

}  // namespace

St67NetworkSession::St67NetworkSession(St67Runtime& runtime) : runtime_(runtime) {
  activeRuntime = &runtime_;
  std::memset(&runtime_.callbacks, 0, sizeof(runtime_.callbacks));
}

bool St67NetworkSession::initialize(bool logModule) {
  if (!credentialsValid()) {
    fail(runtime_, "credentials-unavailable");
    return false;
  }
  runtime_.state = St67State::Starting;
  const uint32_t startedAt = HAL_GetTick();
  if (!runtime_.w6xInitialized) {
    runtime_.lastStatus = W6X_Init();
    if (!logStage(runtime_, "w6x-init", runtime_.lastStatus, startedAt)) {
      fail(runtime_, "w6x-init");
      return false;
    }
    runtime_.w6xInitialized = true;
  }
  if (logModule) {
    W6X_ModuleInfo_t* moduleInfo = W6X_GetModuleInfo();
    if (moduleInfo == nullptr) {
      fail(runtime_, "module-info");
      return false;
    }
    DebugService::instance().logf(DebugService::Level::Info,
                                  "ST67 module=%s sdk=%u.%u.%u.%u",
                                  moduleInfo->ModuleID.ModuleName,
                                  moduleInfo->SDK_Version.Major,
                                  moduleInfo->SDK_Version.Sub1,
                                  moduleInfo->SDK_Version.Sub2,
                                  moduleInfo->SDK_Version.Patch);
  }
  if (!runtime_.wifiInitialized) {
    runtime_.callbacks.APP_wifi_cb = &wifiCallback;
    runtime_.callbacks.APP_error_cb = &errorCallback;
    runtime_.lastStatus = W6X_RegisterAppCb(&runtime_.callbacks);
    if (!logStage(runtime_, "callback-register", runtime_.lastStatus, startedAt)) {
      fail(runtime_, "callback-register");
      return false;
    }
    runtime_.lastStatus = W6X_WiFi_Init();
    if (!logStage(runtime_, "wifi-init", runtime_.lastStatus, startedAt)) {
      fail(runtime_, "wifi-init");
      return false;
    }
    runtime_.wifiInitialized = true;
  }
  if (!runtime_.lwipInitialized) {
    if (MX_LWIP_Init() != 0) {
      fail(runtime_, "lwip-init");
      return false;
    }
    runtime_.lwipInitialized = true;
  }
  if (!St67NetworkInterfacesReady()) {
    fail(runtime_, "lwip-netif");
    return false;
  }
  runtime_.state = St67State::Ready;
  return true;
}

bool St67NetworkSession::open() {
  osThreadFlagsClear(kFlagConnected | kFlagDisconnected | kFlagDriverError);
  W6X_WiFi_Connect_Opts_t options{};
  std::strncpy(reinterpret_cast<char*>(options.SSID), APP_ST67_WIFI_SSID,
               W6X_WIFI_MAX_SSID_SIZE);
  std::strncpy(reinterpret_cast<char*>(options.Password), APP_ST67_WIFI_PASSWORD,
               W6X_WIFI_MAX_PASSWORD_SIZE);
  options.Reconnection_interval = 1U;
  options.Reconnection_nb_attempts = 1U;
  runtime_.state = St67State::Connecting;
  const uint32_t startedAt = HAL_GetTick();
  runtime_.lastStatus = W6X_WiFi_Connect(&options);
  if (!logStage(runtime_, "connect", runtime_.lastStatus, startedAt)) {
    fail(runtime_, "connect");
    disconnect();
    return false;
  }
  W6X_WiFi_StaStateType_e stationState = W6X_WIFI_STATE_STA_OFF;
  W6X_WiFi_Connect_t connection{};
  if (W6X_WiFi_Station_GetState(&stationState, &connection) != W6X_STATUS_OK ||
      stationState != W6X_WIFI_STATE_STA_CONNECTED) {
    fail(runtime_, "connect-state");
    disconnect();
    return false;
  }
  DebugService::instance().logf(DebugService::Level::Info,
                                "ST67 connected ssid=%s channel=%lu rssi=%ld",
                                connection.SSID,
                                static_cast<unsigned long>(connection.Channel),
                                static_cast<long>(connection.Rssi));
  if (!waitForDhcp()) {
    fail(runtime_, "dhcp");
    disconnect();
    return false;
  }
  runtime_.state = St67State::Online;
  return true;
}

bool St67NetworkSession::isStationDisconnected() const {
  return stationDisconnected(runtime_);
}

bool St67NetworkSession::isReady() const {
  return runtime_.state == St67State::Ready && runtime_.w6xInitialized &&
         runtime_.wifiInitialized && runtime_.lwipInitialized &&
      stationDisconnected(runtime_) && St67NetworkInterfacesReady();
}

bool St67NetworkSession::disconnect() {
  if (!runtime_.wifiInitialized || stationDisconnected(runtime_)) {
    return true;
  }
  runtime_.state = St67State::Disconnecting;
  osThreadFlagsClear(kFlagDisconnected | kFlagDriverError);
  runtime_.lastStatus = W6X_WiFi_Disconnect(1U);
  if (runtime_.lastStatus != W6X_STATUS_OK) {
    fail(runtime_, "disconnect");
    return false;
  }
  const uint32_t flags = osThreadFlagsWait(
      kFlagDisconnected | kFlagDriverError, osFlagsWaitAny,
      APP_ST67_DISCONNECT_TIMEOUT_MS);
  if ((flags & kFlagDisconnected) == 0U || !stationDisconnected(runtime_)) {
    fail(runtime_, (flags & kFlagDisconnected) == 0U ? "disconnect" : "link-down");
    return false;
  }
  osDelay(100U);
  if (!stationDisconnected(runtime_)) {
    fail(runtime_, "reconnect");
    return false;
  }
  runtime_.state = St67State::Ready;
  return true;
}

bool St67NetworkSession::stop() {
  if (runtime_.wifiInitialized && !stationDisconnected(runtime_)) {
    disconnect();
  }
  if (runtime_.wifiInitialized) {
    W6X_WiFi_DeInit();
    runtime_.wifiInitialized = false;
  }
  if (runtime_.w6xInitialized) {
    W6X_DeInit();
    runtime_.w6xInitialized = false;
  }
  osDelay(APP_ST67_SHUTDOWN_SETTLING_DELAY_MS);
  if (!finalHardwareState()) {
    fail(runtime_, "final-state");
  }
  runtime_.state = St67State::Off;
  return runtime_.firstFailureStage == nullptr;
}

}  // namespace HostController
