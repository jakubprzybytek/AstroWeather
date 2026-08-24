#ifndef INC_HOSTCONTROLLER_ST67RUNTIME_HPP_
#define INC_HOSTCONTROLLER_ST67RUNTIME_HPP_

#include <stdint.h>

#include "cmsis_os2.h"
#include "app_config.h"
#include "http_client.h"
#include "lwip/dns.h"
#include "w6x_api.h"

#include <HostController/St67FetchTypes.hpp>

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
  bool w6xInitialized = false;
  bool wifiInitialized = false;
  bool lwipInitialized = false;
  bool dnsPending = false;
  err_t dnsStatus = ERR_INPROGRESS;
  ip_addr_t dnsAddress{};
  HTTP_Status_Code_e httpStatus = HTTP_VERSION_NOT_SUPPORTED;
  int32_t httpError = HTTP_CLIENT_ERR;
  uint32_t httpReceivedBytes = 0U;
  uint8_t httpPayload[APP_ST67_HTTP_MAX_RESPONSE_BYTES]{};
  uint32_t httpPayloadLength = 0U;
  St67FetchRequest* clientRequest = nullptr;
  uint32_t clientPayloadLength = 0U;
  bool responseTooLarge = false;
  uint32_t httpCrc = 0U;
  St67State state = St67State::Off;
  const char* firstFailureStage = nullptr;
  W6X_Status_t firstFailureStatus = W6X_STATUS_OK;
};

}  // namespace HostController

#endif /* INC_HOSTCONTROLLER_ST67RUNTIME_HPP_ */
