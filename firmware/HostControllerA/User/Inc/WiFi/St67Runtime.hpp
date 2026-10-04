#ifndef INC_HOSTCONTROLLER_ST67RUNTIME_HPP_
#define INC_HOSTCONTROLLER_ST67RUNTIME_HPP_

#include <stdint.h>

#include "cmsis_os2.h"
#include "app_config.h"
#include "w6x_api.h"

#include <WiFi/HttpClient.hpp>
#include <WiFi/HttpResponseParser.hpp>
#include <WiFi/St67FetchTypes.hpp>

namespace HostController {

enum class St67State : uint8_t {
  Off,
  Starting,
  Ready,
  Connecting,
  Online,
  Disconnecting,
  Complete,
  Fault,
};

struct St67Runtime {
  osThreadId_t taskHandle = nullptr;
  W6X_App_Cb_t callbacks{};
  W6X_Status_t lastStatus = W6X_STATUS_OK;
  const char* lastErrorFunction = nullptr;
  uint32_t lastWifiReason = 0U;
  W6X_event_id_t lastWifiEvent = 0U;
  W6X_event_id_t lastNetEvent = 0U;
  bool w6xInitialized = false;
  bool wifiInitialized = false;
  bool netInitialized = false;
  uint8_t serverIpv4[4]{};  // the API host, resolved by the module's DNS
  uint32_t httpStatus = HttpResponse::kNoStatus;
  int32_t httpError = HttpClient::kErrorResponse;
  uint32_t httpReceivedBytes = 0U;
  uint8_t httpPayload[APP_ST67_HTTP_MAX_RESPONSE_BYTES]{};
  uint32_t httpPayloadLength = 0U;
  St67FetchRequest* clientRequest = nullptr;
  uint32_t clientPayloadLength = 0U;
  bool responseTooLarge = false;
  uint32_t httpCrc = 0U;
  uint32_t httpResponseTick = 0U;
  St67State state = St67State::Off;
  const char* firstFailureStage = nullptr;
  W6X_Status_t firstFailureStatus = W6X_STATUS_OK;
};

// Advances the progress of the client request being served, if any, and wakes
// its waiter. Frozen once a failure is recorded, so the stage keeps naming the
// step that failed while disconnect and cleanup still run.
inline void setFetchStage(St67Runtime& runtime, FetchStage stage) {
  St67FetchRequest* request = runtime.clientRequest;
  if (request == nullptr || runtime.firstFailureStage != nullptr) {
    return;
  }
  request->stage = stage;
  if (request->waiter != nullptr) {
    osThreadFlagsSet(request->waiter, kFetchFlagStage);
  }
}

}  // namespace HostController

#endif /* INC_HOSTCONTROLLER_ST67RUNTIME_HPP_ */
