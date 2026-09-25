#include <WiFi/St67HttpFetcher.hpp>

#include <Debug/LogService.hpp>
#include <WiFi/HttpClient.hpp>
#include <WiFi/HttpResponseParser.hpp>
#include <WiFi/St67HttpFetchTask.hpp>
#include <WiFi/St67HttpRules.hpp>
#include <WiFi/St67Runtime.hpp>
#include <Utils/Crc32.hpp>

#include "app_config.h"
#include "lwip/dns.h"
#include "main.h"

#include <cstring>

namespace HostController {
namespace {

constexpr uint32_t kFlagDns = 1U << 4;
constexpr uint32_t kFlagHttp = 1U << 5;

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
  runtime.httpResponseTick = osKernelGetTickCount();
  const St67HttpRules::ContentTypeCheck contentType = St67HttpRules::checkContentType(
      headers, headerLength, APP_ST67_HTTP_EXPECTED_CONTENT_TYPE);
  if (contentType == St67HttpRules::ContentTypeCheck::Missing) {
    return -1;
  }
  if (contentType == St67HttpRules::ContentTypeCheck::Mismatch) {
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
  runtime.httpCrc = Crc32::update(runtime.httpCrc,
                                  reinterpret_cast<const uint8_t*>(buffer->data),
                                  static_cast<uint32_t>(buffer->length));
  return 0;
}

bool resolveHost(St67Runtime& runtime, const char* host) {
  osThreadFlagsClear(kFlagDns);
  runtime.dnsPending = true;
  runtime.dnsStatus = ERR_INPROGRESS;
  const err_t status = dns_gethostbyname(host, &runtime.dnsAddress,
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
  setFetchStage(runtime_, FetchStage::Downloading);
  target_ = resolveApiTarget(St67CredentialSource());
  LogService::instance().logf(LogService::Level::Debug, "ST67 fetch http://%s%s (%s)",
                              target_.host, target_.path,
                              (target_.hostSaved || target_.pathSaved) ? "saved" : "built-in");
  if (!St67HttpRules::isValidTarget(target_.host, target_.path, HTTP_SNI_MAX_SIZE)) {
    LogService::instance().log(LogService::Level::Error,
                                 "ST67 fetch-config invalid");
    return false;
  }
  const uint32_t startedAt = HAL_GetTick();
  if (!resolveHost(runtime_, target_.host) || !IP_IS_V4(&runtime_.dnsAddress) ||
      ip4_addr_get_u32(ip_2_ip4(&runtime_.dnsAddress)) == 0U) {
    LogService::instance().logf(LogService::Level::Error,
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
  runtime_.httpCrc = Crc32::kInitial;
  runtime_.httpResponseTick = 0U;
  runtime_.clientRequest = request;
  HTTP_connection_t settings{};
  settings.server_name = target_.host;
  settings.timeout = APP_ST67_HTTP_IO_TIMEOUT_MS;
  settings.max_response_len = APP_ST67_HTTP_MAX_RESPONSE_BYTES;
  settings.callback_arg = &runtime_;
  settings.result_fn = &httpResultCallback;
  settings.headers_done_fn = &httpHeadersCallback;
  settings.recv_fn = &httpDataCallback;
  settings.recv_fn_arg = &runtime_;
  const int32_t requestStatus = HttpClient_Get(
      &runtime_.dnsAddress, APP_ST67_HTTP_PORT, target_.host,
      target_.path, &settings);
  if (requestStatus != HTTP_CLIENT_SUCCESS) {
    return false;
  }
  const bool success =
      runtime_.httpError == 0 &&
      HttpResponse::isSuccessStatus(static_cast<uint32_t>(runtime_.httpStatus));
  return success;
}

}  // namespace HostController
