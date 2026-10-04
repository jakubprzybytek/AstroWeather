#include <WiFi/St67HttpFetcher.hpp>

#include <cstdio>
#include <cstring>

#include <Debug/LogService.hpp>
#include <WiFi/HttpClient.hpp>
#include <WiFi/HttpResponseParser.hpp>
#include <WiFi/St67HttpFetchTask.hpp>
#include <WiFi/St67HttpRules.hpp>
#include <WiFi/St67Runtime.hpp>
#include <WiFi/TrustedCa.hpp>
#include <Utils/Crc32.hpp>

#include "app_config.h"
#include "main.h"
#include "w6x_api.h"

namespace HostController {
namespace {

constexpr bool kUseTls = APP_ST67_HTTP_USE_TLS != 0;
constexpr uint16_t kPort = kUseTls ? APP_ST67_HTTPS_PORT : APP_ST67_HTTP_PORT;
constexpr HttpClient::Tls kTrust{TrustedCa::kAnchorName, TrustedCa::kAnchorPem};

void httpResultCallback(void* argument, uint32_t status, uint32_t receivedBytes,
                        int32_t error) {
  St67Runtime& runtime = *static_cast<St67Runtime*>(argument);
  runtime.httpStatus = status;
  runtime.httpReceivedBytes = receivedBytes;
  // A refused Content-Type or an overflowing body already set a reason.
  if (runtime.httpError == HttpClient::kSuccess) {
    runtime.httpError = error;
  }
}

int32_t httpHeadersCallback(void* argument, const uint8_t* headers, uint32_t headerLength,
                            uint32_t contentLength) {
  (void)contentLength;
  St67Runtime& runtime = *static_cast<St67Runtime*>(argument);
  runtime.httpResponseTick = osKernelGetTickCount();
  const St67HttpRules::ContentTypeCheck contentType = St67HttpRules::checkContentType(
      headers, headerLength, APP_ST67_HTTP_EXPECTED_CONTENT_TYPE);
  if (contentType != St67HttpRules::ContentTypeCheck::Match) {
    runtime.httpError = HttpClient::kErrorResponse;
    return -1;
  }
  return 0;
}

int32_t httpBodyCallback(void* argument, const HttpClient::Body& body) {
  St67Runtime& runtime = *static_cast<St67Runtime*>(argument);
  uint8_t* destination = runtime.httpPayload;
  uint32_t* destinationLength = &runtime.httpPayloadLength;
  uint32_t destinationCapacity = sizeof(runtime.httpPayload);
  if (runtime.clientRequest != nullptr) {
    destination = runtime.clientRequest->buffer;
    destinationLength = &runtime.clientPayloadLength;
    destinationCapacity = runtime.clientRequest->capacity;
  }
  if (*destinationLength > destinationCapacity ||
      body.length > destinationCapacity - *destinationLength) {
    runtime.responseTooLarge = true;
    runtime.httpError = HttpClient::kErrorResponse;
    return -1;
  }
  std::memcpy(destination + *destinationLength, body.data, body.length);
  *destinationLength += body.length;
  runtime.httpCrc = Crc32::update(runtime.httpCrc, body.data, body.length);
  return 0;
}

// The module's DNS. Synchronous; the driver bounds it with its own timeout, so
// APP_ST67_DNS_TIMEOUT_MS is not involved.
bool resolveHost(St67Runtime& runtime, const char* host) {
  std::memset(runtime.serverIpv4, 0, sizeof(runtime.serverIpv4));
  if (W6X_Net_ResolveHostAddress(host, runtime.serverIpv4) != W6X_STATUS_OK) {
    return false;
  }
  return ATON(runtime.serverIpv4) != 0U;
}

}  // namespace

St67HttpFetcher::St67HttpFetcher(St67Runtime& runtime) : runtime_(runtime) {}

bool St67HttpFetcher::fetch(St67FetchRequest* request) {
  setFetchStage(runtime_, FetchStage::Downloading);
  target_ = resolveApiTarget(St67CredentialSource());
  if (request->pathOverride != nullptr) {
    std::snprintf(target_.path, sizeof(target_.path), "%s", request->pathOverride);
  }
  // The key itself is never logged.
  LogService::instance().logf(LogService::Level::Debug, "ST67 fetch %s://%s%s key=%s (%s) ca=%s",
                              kUseTls ? "https" : "http", target_.host, target_.path,
                              target_.key[0] != '\0' ? "<set>" : "<none>",
                              request->pathOverride != nullptr ? "one-off path"
                              : (target_.hostSaved || target_.pathSaved || target_.keySaved)
                                  ? "saved" : "built-in",
                              kUseTls ? TrustedCa::kAnchorName : "none");
  // The host doubles as the SNI, which the module caps at W6X_NET_SNI_MAX_SIZE.
  if (!St67HttpRules::isValidTarget(target_.host, target_.path, W6X_NET_SNI_MAX_SIZE) ||
      (target_.key[0] != '\0' &&
       !St67HttpRules::isValidKey(target_.key, Settings::kMaxApiKeyLength)) ||
      !St67HttpRules::formatRequestPath(requestPath_, sizeof(requestPath_), target_.path,
                                        target_.key)) {
    LogService::instance().log(LogService::Level::Error,
                                 "ST67 fetch-config invalid");
    return false;
  }
  uint32_t startedAt = HAL_GetTick();
  if (!resolveHost(runtime_, target_.host)) {
    LogService::instance().logf(LogService::Level::Error,
                                  "ST67 dns failed elapsed=%lums",
                                  static_cast<unsigned long>(HAL_GetTick() - startedAt));
    return false;
  }
  runtime_.httpStatus = HttpResponse::kNoStatus;
  runtime_.httpError = HttpClient::kSuccess;
  runtime_.httpReceivedBytes = 0U;
  runtime_.httpPayloadLength = 0U;
  runtime_.clientPayloadLength = 0U;
  runtime_.responseTooLarge = false;
  runtime_.httpCrc = Crc32::kInitial;
  runtime_.httpResponseTick = 0U;
  runtime_.clientRequest = request;

  HttpClient::Request http{};
  std::memcpy(http.serverIpv4, runtime_.serverIpv4, sizeof(http.serverIpv4));
  http.port = kPort;
  http.host = target_.host;
  http.path = requestPath_;
  http.tls = kUseTls ? &kTrust : nullptr;
  http.ioTimeoutMs = APP_ST67_HTTP_IO_TIMEOUT_MS;
  http.totalTimeoutMs = APP_ST67_HTTP_TOTAL_TIMEOUT_MS;
  http.maxResponseLength = APP_ST67_HTTP_MAX_RESPONSE_BYTES;
  http.onHeaders = &httpHeadersCallback;
  http.onBody = &httpBodyCallback;
  http.onResult = &httpResultCallback;
  http.arg = &runtime_;
  startedAt = HAL_GetTick();
  const int32_t requestStatus = HttpClient::get(http);
  const bool success = requestStatus == HttpClient::kSuccess &&
                       runtime_.httpError == HttpClient::kSuccess &&
                       HttpResponse::isSuccessStatus(runtime_.httpStatus);
  if (!success) {
    // lastErrorFunction names the driver call the error callback reported last,
    // which says which AT exchange failed inside the client's step.
    LogService::instance().logf(
        LogService::Level::Error,
        "ST67 %s failed: %s status=%lu bytes=%lu elapsed=%lums w6x=%d(%s) in %s",
        kUseTls ? "https" : "http",
        HttpClient::resultName(requestStatus != HttpClient::kSuccess ? requestStatus
                                                                     : runtime_.httpError),
        static_cast<unsigned long>(runtime_.httpStatus),
        static_cast<unsigned long>(runtime_.httpReceivedBytes),
        static_cast<unsigned long>(HAL_GetTick() - startedAt),
        static_cast<int>(runtime_.lastStatus), W6X_StatusToStr(runtime_.lastStatus),
        runtime_.lastErrorFunction != nullptr ? runtime_.lastErrorFunction : "-");
  }
  return success;
}

}  // namespace HostController
