#include <WiFi/St67NetworkSession.hpp>

#include <Debug/LogService.hpp>
#include <WiFi/St67ConnectDiagnosis.hpp>
#include <WiFi/St67NetworkAdapter.hpp>
#include <WiFi/St67HttpFetchTask.hpp>
#include <WiFi/St67Runtime.hpp>
#include <Settings/SettingsStore.hpp>

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

Settings::Store* credentialSource = nullptr;
WifiConnectSummary lastConnect{};

using St67ConnectDiagnosis::kNoReason;
using St67ConnectDiagnosis::SsidVisibility;

static_assert(St67ConnectDiagnosis::kReasonAuthenticationFailure ==
                  WLAN_FW_AUTHENTICATION_FAILURE &&
              St67ConnectDiagnosis::kReasonAuthAlgoFailure == WLAN_FW_AUTH_ALGO_FAILURE &&
              St67ConnectDiagnosis::kReasonDeauthByApWhenConnection ==
                  WLAN_FW_DEAUTH_BY_AP_WHEN_CONNECTION &&
              St67ConnectDiagnosis::kReasonPskHandshakeTimeout ==
                  WLAN_FW_4WAY_HANDSHAKE_ERROR_PSK_TIMEOUT_FAILURE &&
              St67ConnectDiagnosis::kReasonNoBssidAndChannel ==
                  WLAN_FW_SCAN_NO_BSSID_AND_CHANNEL &&
              St67ConnectDiagnosis::kReasonSecurityNoMatch == WLAN_FW_NETWORK_SECURITY_NOMATCH,
              "St67ConnectDiagnosis reason codes must match w6x_types.h");

// Bits 0-5 of the fetch task's flags are taken by the trigger, the session and
// the HTTP fetcher.
constexpr uint32_t kFlagScanDone = 1U << 6;
constexpr uint32_t kScanTimeoutMs = 8000U;

volatile int32_t scanCount = -1;

void scanCallback(int32_t status, W6X_WiFi_Scan_Result_t* entry) {
  scanCount = (status == 0 && entry != nullptr) ? static_cast<int32_t>(entry->Count) : -1;
  if (activeRuntime != nullptr && activeRuntime->taskHandle != nullptr) {
    osThreadFlagsSet(activeRuntime->taskHandle, kFlagScanDone);
  }
}

// A missing network and a silent one look the same to the station: the
// connect just times out with no reason code. Scanning for the SSID tells them
// apart, which is what lets a mistyped SSID be reported as one.
SsidVisibility scanForSsid(const char* ssid) {
  W6X_WiFi_Scan_Opts_t options{};
  std::strncpy(reinterpret_cast<char*>(options.SSID), ssid, W6X_WIFI_MAX_SSID_SIZE);
  options.Scan_type = W6X_WIFI_SCAN_ACTIVE;  // active, so a hidden SSID still answers
  options.MaxCnt = 5U;
  scanCount = -1;
  osThreadFlagsClear(kFlagScanDone);
  if (W6X_WiFi_Scan(&options, &scanCallback) != W6X_STATUS_OK) {
    return SsidVisibility::Unknown;
  }
  const uint32_t flags = osThreadFlagsWait(kFlagScanDone, osFlagsWaitAny, kScanTimeoutMs);
  if ((flags & osFlagsError) != 0U || scanCount < 0) {
    LogService::instance().logf(LogService::Level::Debug, "WiFi scan for '%s' did not complete",
                                ssid);
    return SsidVisibility::Unknown;
  }
  LogService::instance().logf(LogService::Level::Debug,
                              "WiFi scan for '%s' found %ld access point(s)", ssid,
                              static_cast<long>(scanCount));
  return (scanCount > 0) ? SsidVisibility::Visible : SsidVisibility::NotVisible;
}

struct Credentials {
  char ssid[W6X_WIFI_MAX_SSID_SIZE + 1U] = {};
  char password[W6X_WIFI_MAX_PASSWORD_SIZE + 1U] = {};
};

bool loadCredentials(Credentials& credentials) {
  if (credentialSource != nullptr) {
    credentialSource->copyWifiCredentials(credentials.ssid, sizeof(credentials.ssid),
                                          credentials.password,
                                          sizeof(credentials.password));
  }
  return credentials.ssid[0] != '\0';
}

void recordConnect(WifiConnectResult result, const char* ssid, uint32_t reason = kNoReason,
                   int32_t rssi = 0, uint32_t channel = 0U) {
  WifiConnectSummary summary{};
  summary.result = result;
  summary.reason = reason;
  summary.reasonText = (reason == kNoReason) ? "" : W6X_WiFi_ReasonToStr(&reason);
  summary.tick = osKernelGetTickCount();
  summary.rssi = rssi;
  summary.channel = channel;
  std::strncpy(summary.ssid, (ssid != nullptr) ? ssid : "", sizeof(summary.ssid) - 1U);
  taskENTER_CRITICAL();
  lastConnect = summary;
  taskEXIT_CRITICAL();
}

// One plain-language line per outcome, saying what to check. The password is
// never logged.
void reportConnectFailure(WifiConnectResult result, const char* ssid, uint32_t reason) {
  LogService& log = LogService::instance();
  const char* format = St67ConnectDiagnosis::failureMessageFormat(result);
  switch (St67ConnectDiagnosis::failureMessageArguments(result)) {
    case St67ConnectDiagnosis::MessageArguments::None:
      log.log(LogService::Level::Error, format);
      break;
    case St67ConnectDiagnosis::MessageArguments::Ssid:
      log.logf(LogService::Level::Error, format, ssid);
      break;
    case St67ConnectDiagnosis::MessageArguments::SsidAndReason:
      log.logf(LogService::Level::Error, format, ssid,
               (reason == kNoReason) ? "no reason given" : W6X_WiFi_ReasonToStr(&reason),
               static_cast<unsigned long>(reason));
      break;
  }
}

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
  LogService::instance().logf(
      status == W6X_STATUS_OK ? LogService::Level::Debug
                              : LogService::Level::Error,
      "ST67 %s status=%d(%s) elapsed=%lums", stage, static_cast<int>(status),
      W6X_StatusToStr(status),
      static_cast<unsigned long>(HAL_GetTick() - startedAt));
  return status == W6X_STATUS_OK;
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
  // Checked before powering the module up, so an unconfigured device fails
  // fast with a message that says what to do.
  Credentials credentials{};
  if (!loadCredentials(credentials)) {
    recordConnect(WifiConnectResult::NoCredentials, "");
    reportConnectFailure(WifiConnectResult::NoCredentials, "", kNoReason);
    fail(runtime_, "credentials");
    return false;
  }
  setFetchStage(runtime_, FetchStage::StartingModule);
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
    LogService::instance().logf(LogService::Level::Info,
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
  // Read on every connect, so 'wifi set' takes effect on the next attempt.
  Credentials credentials{};
  if (!loadCredentials(credentials)) {
    recordConnect(WifiConnectResult::NoCredentials, "");
    reportConnectFailure(WifiConnectResult::NoCredentials, "", kNoReason);
    fail(runtime_, "credentials");
    return false;
  }
  W6X_WiFi_Connect_Opts_t options{};
  std::memcpy(options.SSID, credentials.ssid, sizeof(credentials.ssid));
  std::memcpy(options.Password, credentials.password, sizeof(credentials.password));
  std::memset(credentials.password, 0, sizeof(credentials.password));
  options.Reconnection_interval = 1U;
  options.Reconnection_nb_attempts = 1U;
  setFetchStage(runtime_, FetchStage::JoiningWifi);
  runtime_.state = St67State::Connecting;
  runtime_.lastWifiReason = kNoReason;
  const uint32_t startedAt = HAL_GetTick();
  runtime_.lastStatus = W6X_WiFi_Connect(&options);
  std::memset(options.Password, 0, sizeof(options.Password));
  if (!logStage(runtime_, "connect", runtime_.lastStatus, startedAt)) {
    const uint32_t reason = runtime_.lastWifiReason;
    LogService::instance().logf(LogService::Level::Debug, "WiFi connect reason=%s",
                                (reason == kNoReason) ? "none" : W6X_WiFi_ReasonToStr(
                                    const_cast<uint32_t*>(&reason)));
    WifiConnectResult result = St67ConnectDiagnosis::classifyConnectFailure(reason);
    if (St67ConnectDiagnosis::needsScan(result)) {
      result = St67ConnectDiagnosis::applyScan(result, scanForSsid(credentials.ssid));
    }
    recordConnect(result, credentials.ssid, reason);
    reportConnectFailure(result, credentials.ssid, reason);
    fail(runtime_, "connect");
    disconnect();
    return false;
  }
  W6X_WiFi_StaStateType_e stationState = W6X_WIFI_STATE_STA_OFF;
  W6X_WiFi_Connect_t connection{};
  if (W6X_WiFi_Station_GetState(&stationState, &connection) != W6X_STATUS_OK ||
      stationState != W6X_WIFI_STATE_STA_CONNECTED) {
    recordConnect(WifiConnectResult::Failed, credentials.ssid, runtime_.lastWifiReason);
    reportConnectFailure(WifiConnectResult::Failed, credentials.ssid, runtime_.lastWifiReason);
    fail(runtime_, "connect-state");
    disconnect();
    return false;
  }
  LogService::instance().logf(LogService::Level::Info,
                                "ST67 connected ssid=%s channel=%lu rssi=%ld",
                                connection.SSID,
                                static_cast<unsigned long>(connection.Channel),
                                static_cast<long>(connection.Rssi));
  setFetchStage(runtime_, FetchStage::GettingIp);
  if (!waitForDhcp()) {
    recordConnect(WifiConnectResult::DhcpFailed, credentials.ssid);
    reportConnectFailure(WifiConnectResult::DhcpFailed, credentials.ssid, kNoReason);
    fail(runtime_, "dhcp");
    disconnect();
    return false;
  }
  recordConnect(WifiConnectResult::Connected, credentials.ssid, kNoReason,
                static_cast<int32_t>(connection.Rssi),
                static_cast<uint32_t>(connection.Channel));
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
  setFetchStage(runtime_, FetchStage::Disconnecting);
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

void SetSt67CredentialSource(Settings::Store* store) { credentialSource = store; }

Settings::Store* St67CredentialSource() { return credentialSource; }

WifiConnectSummary LastWifiConnect() {
  taskENTER_CRITICAL();
  const WifiConnectSummary summary = lastConnect;
  taskEXIT_CRITICAL();
  return summary;
}

const char* wifiConnectResultName(WifiConnectResult result) {
  switch (result) {
    case WifiConnectResult::NeverTried: return "not tried";
    case WifiConnectResult::Connected: return "connected";
    case WifiConnectResult::NoCredentials: return "no credentials";
    case WifiConnectResult::NetworkNotFound: return "network not found";
    case WifiConnectResult::WrongPassword: return "wrong password";
    case WifiConnectResult::SecurityMismatch: return "authentication refused";
    case WifiConnectResult::NoResponse: return "in range but no response";
    case WifiConnectResult::NoResponseUnchecked: return "no response";
    case WifiConnectResult::DhcpFailed: return "no IP from DHCP";
    case WifiConnectResult::Failed: return "failed";
  }
  return "unknown";
}

}  // namespace HostController
