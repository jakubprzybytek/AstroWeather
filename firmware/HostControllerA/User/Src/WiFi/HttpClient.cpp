#include <WiFi/HttpClient.hpp>

#include <WiFi/HttpResponseParser.hpp>

#include "FreeRTOS.h"
#include "main.h"
#include "w6x_api.h"

#include <cstdio>
#include <cstring>

namespace HostController {
namespace HttpClient {
namespace {

using HttpResponse::kHeaderCapacity;
constexpr uint32_t kChunkCapacity = 1024U;
constexpr uint32_t kRequestCapacity = 512U;
// The driver splits the list on commas; one entry keeps the server on HTTP/1.1.
constexpr char kAlpn[] = "http/1.1";

static_assert(HttpResponse::kNoStatus == HTTP_VERSION_NOT_SUPPORTED,
              "the parser's no-status code must match W6X_HTTP_Status_Code_e");

// One socket and, with TLS, the certificate registered under the socket's
// number as its security tag, the way the driver's own HTTP client does it.
struct Connection {
  int32_t socket = -1;
  bool credentialAdded = false;
};

void close(Connection& connection) {
  if (connection.socket >= 0) {
    (void)W6X_Net_Close(connection.socket);
  }
  if (connection.credentialAdded) {
    (void)W6X_Net_TLS_Credential_Delete(static_cast<uint32_t>(connection.socket),
                                        W6X_NET_TLS_CREDENTIAL_CA_CERTIFICATE);
  }
  connection.socket = -1;
  connection.credentialAdded = false;
}

bool setOption(int32_t socket, int32_t level, int32_t option, const void* value,
               uint32_t length) {
  return W6X_Net_Setsockopt(socket, level, option, value, length) == 0;
}

// Uploads the CA, selects it for this socket, and sets SNI and ALPN. The
// module does the verification: with a CA set the driver requests server
// authentication (AT+CIPSSLCCONF auth mode 2).
bool configureTls(Connection& connection, const Request& request) {
  const uint32_t tag = static_cast<uint32_t>(connection.socket);
  if (W6X_Net_TLS_Credential_AddByContent(
          tag, W6X_NET_TLS_CREDENTIAL_CA_CERTIFICATE, request.tls->name,
          request.tls->pem, static_cast<uint32_t>(std::strlen(request.tls->pem))) != 0) {
    return false;
  }
  connection.credentialAdded = true;
  const uint8_t tags[1] = {static_cast<uint8_t>(connection.socket)};
  return setOption(connection.socket, SOL_TLS, TLS_SEC_TAG_LIST, tags, sizeof(tags)) &&
         setOption(connection.socket, SOL_TLS, TLS_HOSTNAME, request.host,
                   static_cast<uint32_t>(std::strlen(request.host))) &&
         setOption(connection.socket, SOL_TLS, TLS_ALPN_LIST, kAlpn, sizeof(kAlpn));
}

bool sendRequest(int32_t socket, const Request& request) {
  char* buffer = static_cast<char*>(pvPortMalloc(kRequestCapacity));
  if (buffer == nullptr) {
    return false;
  }
  const int length = std::snprintf(
      buffer, kRequestCapacity,
      "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", request.path,
      request.host);
  bool sent = false;
  if (length > 0 && static_cast<uint32_t>(length) < kRequestCapacity) {
    sent = W6X_Net_Send(socket, buffer, static_cast<size_t>(length), 0) == length;
  }
  vPortFree(buffer);
  return sent;
}

bool deadlinePassed(uint32_t deadline) {
  return static_cast<int32_t>(HAL_GetTick() - deadline) >= 0;
}

// Reads and parses the response. Returns kSuccess or the error, with
// *statusCode and *received filled in for the result callback.
int32_t receiveResponse(int32_t socket, const Request& request, uint32_t deadline,
                        uint32_t* statusCode, uint32_t* received) {
  uint8_t* headerBuffer = static_cast<uint8_t*>(pvPortMalloc(kHeaderCapacity + 1U));
  uint8_t* chunk = static_cast<uint8_t*>(pvPortMalloc(kChunkCapacity));
  if (headerBuffer == nullptr || chunk == nullptr) {
    vPortFree(headerBuffer);
    vPortFree(chunk);
    return kErrorSocket;
  }
  std::memset(headerBuffer, 0, kHeaderCapacity + 1U);
  uint32_t headerLength = 0U;
  uint32_t bodyOffset = 0U;
  HttpResponse::Head head{};
  bool headerComplete = false;
  int32_t result = kErrorResponse;

  while (true) {
    if (deadlinePassed(deadline)) {
      result = kErrorTimeout;
      break;
    }
    // >0 data; 0 the per-read timeout, so the total deadline is what bounds
    // the loop; <0 the peer closed or the socket failed, which the driver
    // does not tell apart.
    const int32_t count =
        static_cast<int32_t>(W6X_Net_Recv(socket, chunk, kChunkCapacity, 0));
    if (count == 0) {
      continue;
    }
    if (count < 0) {
      if (headerComplete && HttpResponse::closeEndsBody(head)) {
        result = kSuccess;
      }
      break;
    }
    uint32_t chunkOffset = 0U;
    if (!headerComplete) {
      const HttpResponse::HeaderProgress progress = HttpResponse::appendHeaderBytes(
          headerBuffer, kHeaderCapacity, &headerLength, chunk,
          static_cast<uint32_t>(count), request.maxResponseLength, &head,
          &bodyOffset, &chunkOffset);
      if (progress == HttpResponse::HeaderProgress::NeedMore) {
        continue;
      }
      if (progress != HttpResponse::HeaderProgress::Complete) {
        break;
      }
      if (request.onHeaders != nullptr &&
          request.onHeaders(request.arg, headerBuffer, bodyOffset,
                            head.contentLength) < 0) {
        break;
      }
      headerComplete = true;
    }
    const uint32_t bodyLength = static_cast<uint32_t>(count) - chunkOffset;
    if (bodyLength > 0U) {
      const Body body{chunk + chunkOffset, bodyLength};
      if (!HttpResponse::acceptsBody(head, *received, bodyLength,
                                     request.maxResponseLength) ||
          request.onBody == nullptr || request.onBody(request.arg, body) < 0) {
        break;
      }
      *received += bodyLength;
    }
    if (HttpResponse::isBodyComplete(head, *received)) {
      result = kSuccess;
      break;
    }
  }

  vPortFree(headerBuffer);
  vPortFree(chunk);
  *statusCode = head.statusCode;
  if (result == kSuccess &&
      (!headerComplete ||
       (head.hasContentLength && !HttpResponse::isBodyComplete(head, *received)))) {
    result = kErrorResponse;
  }
  return result;
}

}  // namespace

const char* resultName(int32_t result) {
  switch (result) {
    case kSuccess: return "ok";
    case kBadParam: return "bad-param";
    case kErrorSocket: return "socket";
    case kErrorTls: return "tls-setup";
    case kErrorConnect: return "connect";
    case kErrorSend: return "send";
    case kErrorResponse: return "response";
    case kErrorTimeout: return "timeout";
  }
  return "unknown";
}

int32_t get(const Request& request) {
  if (request.host == nullptr || request.path == nullptr ||
      request.maxResponseLength == 0U ||
      (request.tls != nullptr &&
       (request.tls->name == nullptr || request.tls->pem == nullptr))) {
    return kBadParam;
  }

  uint32_t statusCode = HttpResponse::kNoStatus;
  uint32_t received = 0U;
  int32_t result = kErrorSocket;
  Connection connection{};
  connection.socket = W6X_Net_Socket(
      AF_INET, SOCK_STREAM, request.tls != nullptr ? IPPROTO_TLS_1_2 : IPPROTO_TCP);
  if (connection.socket >= 0) {
    const int32_t ioTimeout = static_cast<int32_t>(request.ioTimeoutMs);
    if (!setOption(connection.socket, SOL_SOCKET, SO_RCVTIMEO, &ioTimeout,
                   sizeof(ioTimeout)) ||
        !setOption(connection.socket, SOL_SOCKET, SO_SNDTIMEO, &ioTimeout,
                   sizeof(ioTimeout))) {
      result = kErrorSocket;
    } else if (request.tls != nullptr && !configureTls(connection, request)) {
      result = kErrorTls;
    } else {
      // The handshake happens inside connect on a TLS socket, so the total
      // deadline starts here.
      const uint32_t deadline = HAL_GetTick() + request.totalTimeoutMs;
      sockaddr_in address{};
      address.sin_len = sizeof(address);
      address.sin_family = AF_INET;
      address.sin_port = PP_HTONS(request.port);
      address.sin_addr.s_addr = ATON(request.serverIpv4);
      if (W6X_Net_Connect(connection.socket, reinterpret_cast<const sockaddr*>(&address),
                          sizeof(address)) != 0) {
        result = kErrorConnect;
      } else if (!sendRequest(connection.socket, request)) {
        result = kErrorSend;
      } else {
        result = receiveResponse(connection.socket, request, deadline, &statusCode,
                                 &received);
      }
    }
  }
  close(connection);

  if (request.onResult != nullptr) {
    request.onResult(request.arg, statusCode, received, result);
  }
  return result;
}

}  // namespace HttpClient
}  // namespace HostController
