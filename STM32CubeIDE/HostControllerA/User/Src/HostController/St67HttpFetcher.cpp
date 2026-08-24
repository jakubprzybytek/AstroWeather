#include <HostController/St67HttpFetcher.hpp>

#include <Debug/DebugService.hpp>
#include <HostController/St67Runtime.hpp>

#include "app_config.h"
#include "lwip/dns.h"
#include "main.h"

#include "FreeRTOS.h"
#include "task.h"

#include <cstring>

namespace HostController {
namespace {

constexpr uint32_t kFlagDns = 1U << 4;
constexpr uint32_t kFlagHttp = 1U << 5;

const char* findBounded(const uint8_t* data, uint16_t length, const char* needle) {
  const size_t needleLength = std::strlen(needle);
  if (needleLength == 0U || needleLength > length) {
    return nullptr;
  }
  for (uint16_t offset = 0U; offset <= length - needleLength; ++offset) {
    if (std::memcmp(data + offset, needle, needleLength) == 0) {
      return reinterpret_cast<const char*>(data + offset);
    }
  }
  return nullptr;
}

void dnsCallback(const char* name, const ip_addr_t* address, void* argument) {
  (void)name;
  St67Runtime& runtime = *static_cast<St67Runtime*>(argument);
  runtime.dnsPending = false;
  runtime.dnsStatus = address != nullptr ? ERR_OK : ERR_VAL;
  if (address != nullptr) {
    runtime.dnsAddress = *address;
  }
  if (runtime.taskHandle != nullptr) {
    osThreadFlagsSet(runtime.taskHandle, kFlagDns);
  }
}

void httpResultCallback(void* argument, HTTP_Status_Code_e status,
                       uint32_t receivedBytes, uint32_t serverResult,
                       int32_t error) {
  (void)serverResult;
  St67Runtime& runtime = *static_cast<St67Runtime*>(argument);
  runtime.httpStatus = status;
  runtime.httpReceivedBytes = receivedBytes;
  runtime.httpError = error;
  if (runtime.taskHandle != nullptr) {
    osThreadFlagsSet(runtime.taskHandle, kFlagHttp);
  }
}

int32_t httpHeadersCallback(HTTP_state_t* connection, void* argument,
                            uint8_t* headers, uint16_t headerLength,
                            uint32_t contentLength) {
  (void)connection;
  (void)contentLength;
  St67Runtime& runtime = *static_cast<St67Runtime*>(argument);
  const char* contentType = findBounded(headers, headerLength, "Content-Type:");
  if (contentType == nullptr) {
    return -1;
  }
  const char* value = contentType + std::strlen("Content-Type:");
  const char* end = reinterpret_cast<const char*>(headers) + headerLength;
  while (value < end && (*value == ' ' || *value == '\t')) {
    ++value;
  }
  const size_t expectedLength = std::strlen(APP_ST67_HTTP_EXPECTED_CONTENT_TYPE);
  if (value + expectedLength > end ||
      std::memcmp(value, APP_ST67_HTTP_EXPECTED_CONTENT_TYPE, expectedLength) != 0) {
    runtime.httpError = HTTP_CLIENT_BAD_PARAM;
    return -1;
  }
  return 0;
}

int32_t httpDataCallback(void* argument, HTTP_buffer_t* buffer, int32_t error) {
  St67Runtime& runtime = *static_cast<St67Runtime*>(argument);
  if (error != 0 || buffer == nullptr || buffer->data == nullptr ||
      buffer->length < 0) {
    return -1;
  }
  uint8_t* destination = runtime.httpPayload;
  uint32_t* destinationLength = &runtime.httpPayloadLength;
  uint32_t destinationCapacity = sizeof(runtime.httpPayload);
  if (runtime.clientRequest != nullptr) {
    destination = runtime.clientRequest->buffer;
    destinationLength = &runtime.clientPayloadLength;
    destinationCapacity = runtime.clientRequest->capacity;
  }
  if (*destinationLength > destinationCapacity ||
      static_cast<uint32_t>(buffer->length) > destinationCapacity - *destinationLength) {
    runtime.responseTooLarge = true;
    runtime.httpError = HTTP_CLIENT_BAD_PARAM;
    return -1;
  }
  std::memcpy(destination + *destinationLength, buffer->data,
              static_cast<size_t>(buffer->length));
  *destinationLength += static_cast<uint32_t>(buffer->length);
  runtime.httpReceivedBytes += static_cast<uint32_t>(buffer->length);
  for (int32_t index = 0; index < buffer->length; ++index) {
    runtime.httpCrc ^= buffer->data[index];
    for (uint32_t bit = 0U; bit < 8U; ++bit) {
      runtime.httpCrc = (runtime.httpCrc & 1U) != 0U
                            ? (runtime.httpCrc >> 1U) ^ 0xEDB88320U
                            : (runtime.httpCrc >> 1U);
    }
  }
  return 0;
}

bool resolveHost(St67Runtime& runtime) {
  osThreadFlagsClear(kFlagDns);
  runtime.dnsPending = true;
  runtime.dnsStatus = ERR_INPROGRESS;
  const err_t status = dns_gethostbyname(APP_ST67_HTTP_HOST, &runtime.dnsAddress,
                                         &dnsCallback, &runtime);
  if (status == ERR_OK) {
    runtime.dnsPending = false;
    return true;
  }
  if (status != ERR_INPROGRESS) {
    runtime.dnsPending = false;
    runtime.dnsStatus = status;
    return false;
  }
  const uint32_t flags = osThreadFlagsWait(kFlagDns, osFlagsWaitAny,
                                           APP_ST67_DNS_TIMEOUT_MS);
  return (flags & kFlagDns) != 0U && !runtime.dnsPending &&
         runtime.dnsStatus == ERR_OK;
}

}  // namespace

St67HttpFetcher::St67HttpFetcher(St67Runtime& runtime) : runtime_(runtime) {}

bool St67HttpFetcher::fetch(St67FetchRequest* request) {
  if (std::strlen(APP_ST67_HTTP_HOST) == 0U ||
      std::strlen(APP_ST67_HTTP_HOST) > HTTP_SNI_MAX_SIZE ||
      std::strstr(APP_ST67_HTTP_HOST, "://") != nullptr ||
      std::strchr(APP_ST67_HTTP_HOST, ':') != nullptr ||
      std::strchr(APP_ST67_HTTP_HOST, '\r') != nullptr ||
      std::strchr(APP_ST67_HTTP_HOST, '\n') != nullptr ||
      std::strlen(APP_ST67_HTTP_PATH) == 0U || APP_ST67_HTTP_PATH[0] != '/' ||
      std::strchr(APP_ST67_HTTP_PATH, '\r') != nullptr ||
      std::strchr(APP_ST67_HTTP_PATH, '\n') != nullptr) {
    DebugService::instance().log(DebugService::Level::Error,
                                 "ST67 fetch-config invalid");
    return false;
  }
  const uint32_t startedAt = HAL_GetTick();
  if (!resolveHost(runtime_) || !IP_IS_V4(&runtime_.dnsAddress) ||
      ip4_addr_get_u32(ip_2_ip4(&runtime_.dnsAddress)) == 0U) {
    DebugService::instance().logf(DebugService::Level::Error,
                                  "ST67 dns failed elapsed=%lums",
                                  static_cast<unsigned long>(HAL_GetTick() - startedAt));
    return false;
  }
  osThreadFlagsClear(kFlagHttp);
  runtime_.httpStatus = HTTP_VERSION_NOT_SUPPORTED;
  runtime_.httpError = HTTP_CLIENT_ERR;
  runtime_.httpReceivedBytes = 0U;
  runtime_.httpPayloadLength = 0U;
  runtime_.clientPayloadLength = 0U;
  runtime_.responseTooLarge = false;
  runtime_.httpCrc = 0xFFFFFFFFU;
  runtime_.clientRequest = request;
  HTTP_connection_t settings{};
  settings.server_name = const_cast<char*>(APP_ST67_HTTP_HOST);
  settings.timeout = APP_ST67_HTTP_IO_TIMEOUT_MS;
  settings.max_response_len = APP_ST67_HTTP_MAX_RESPONSE_BYTES;
  settings.callback_arg = &runtime_;
  settings.result_fn = &httpResultCallback;
  settings.headers_done_fn = &httpHeadersCallback;
  settings.recv_fn = &httpDataCallback;
  settings.recv_fn_arg = &runtime_;
  const int32_t requestStatus = HTTP_Client_Request(
      &runtime_.dnsAddress, APP_ST67_HTTP_PORT, APP_ST67_HTTP_PATH, HTTP_REQ_TYPE_GET,
      nullptr, 0U, nullptr, nullptr, nullptr, nullptr, &settings);
  if (requestStatus != HTTP_CLIENT_SUCCESS) {
    return false;
  }
  const uint32_t flags = osThreadFlagsWait(kFlagHttp, osFlagsWaitAny,
                                           APP_ST67_HTTP_TOTAL_TIMEOUT_MS);
  if ((flags & kFlagHttp) == 0U) {
    HTTP_Client_Cancel();
    const uint32_t deadline = HAL_GetTick() + APP_ST67_HTTP_IO_TIMEOUT_MS;
    while (!HTTP_Client_IsIdle() && static_cast<int32_t>(HAL_GetTick() - deadline) < 0) {
      osDelay(10U);
    }
    return false;
  }
  const uint32_t cleanupDeadline = HAL_GetTick() + APP_ST67_HTTP_IO_TIMEOUT_MS;
  while (!HTTP_Client_IsIdle() &&
         static_cast<int32_t>(HAL_GetTick() - cleanupDeadline) < 0) {
    osDelay(10U);
  }
  const bool success = HTTP_Client_IsIdle() && runtime_.httpError == 0 &&
                       runtime_.httpStatus >= OK && runtime_.httpStatus < 300;
  return success;
}

}  // namespace HostController
