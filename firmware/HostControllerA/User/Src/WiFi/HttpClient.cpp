#include <WiFi/HttpClient.hpp>

#include <WiFi/HttpResponseParser.hpp>

#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "FreeRTOS.h"

#include <cstdio>
#include <cstring>

namespace HostController {
namespace {

using HttpResponse::kHeaderCapacity;
constexpr uint32_t kChunkCapacity = 1024U;

static_assert(HttpResponse::kNoStatus == HTTP_VERSION_NOT_SUPPORTED,
              "the parser's no-status code must match http_client.h");

void notifyFailure(const HTTP_connection_t* settings, HTTP_Status_Code_e status,
                   uint32_t received) {
  if (settings->recv_fn != nullptr) {
    (void)settings->recv_fn(settings->recv_fn_arg, nullptr, HTTP_CLIENT_ERR);
  }
  if (settings->result_fn != nullptr) {
    settings->result_fn(settings->callback_arg, status, received, 0U,
                        HTTP_CLIENT_ERR);
  }
}

}  // namespace

int32_t HttpClient_Get(const ip_addr_t* serverAddress, uint16_t port,
                       const char* host, const char* path,
                       const HTTP_connection_t* settings) {
  if (serverAddress == nullptr || host == nullptr || path == nullptr ||
      settings == nullptr || settings->max_response_len == 0U) {
    return HTTP_CLIENT_BAD_PARAM;
  }

  int32_t socketHandle = socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
  if (socketHandle < 0) {
    return HTTP_CLIENT_ERR;
  }
  timeval timeout{};
  timeout.tv_sec = static_cast<long>(settings->timeout / 1000U);
  timeout.tv_usec = static_cast<long>((settings->timeout % 1000U) * 1000U);
  (void)setsockopt(socketHandle, SOL_SOCKET, SO_RCVTIMEO, &timeout,
                   sizeof(timeout));
  (void)setsockopt(socketHandle, SOL_SOCKET, SO_SNDTIMEO, &timeout,
                   sizeof(timeout));

  sockaddr_in address{};
  address.sin_family = AF_INET;
  address.sin_port = htons(port);
  address.sin_addr.s_addr = ip4_addr_get_u32(ip_2_ip4(serverAddress));
  if (connect(socketHandle, reinterpret_cast<sockaddr*>(&address),
              sizeof(address)) != 0) {
    closesocket(socketHandle);
    return HTTP_CLIENT_ERR;
  }

  char* request = static_cast<char*>(pvPortMalloc(512U));
  if (request == nullptr) {
    closesocket(socketHandle);
    return HTTP_CLIENT_ERR;
  }
  int requestLength = std::snprintf(
      request, 512U,
      "GET %s HTTP/1.1\r\nHost: %s\r\nConnection: close\r\n\r\n", path,
      host);
  if (requestLength <= 0 || static_cast<size_t>(requestLength) >= 512U ||
      send(socketHandle, request, requestLength, 0) != requestLength) {
    vPortFree(request);
    closesocket(socketHandle);
    return HTTP_CLIENT_ERR;
  }
  vPortFree(request);

  uint8_t* headerBuffer = static_cast<uint8_t*>(pvPortMalloc(kHeaderCapacity + 1U));
  uint8_t* chunk = static_cast<uint8_t*>(pvPortMalloc(kChunkCapacity));
  if (headerBuffer == nullptr || chunk == nullptr) {
    vPortFree(headerBuffer);
    vPortFree(chunk);
    closesocket(socketHandle);
    return HTTP_CLIENT_ERR;
  }
  std::memset(headerBuffer, 0, kHeaderCapacity + 1U);
  uint32_t headerLength = 0U;
  uint32_t bodyOffset = 0U;
  uint32_t received = 0U;
  HttpResponse::Head head{};
  bool headerComplete = false;
  int32_t result = HTTP_CLIENT_ERR;

  while (true) {
    int32_t count = recv(socketHandle, chunk, kChunkCapacity, 0);
    if (count == 0) {
      if (headerComplete && HttpResponse::closeEndsBody(head)) {
        result = HTTP_CLIENT_SUCCESS;
      }
      break;
    }
    if (count < 0) {
      break;
    }
    uint32_t chunkOffset = 0U;
    if (!headerComplete) {
      const HttpResponse::HeaderProgress progress = HttpResponse::appendHeaderBytes(
          headerBuffer, kHeaderCapacity, &headerLength, chunk,
          static_cast<uint32_t>(count), settings->max_response_len, &head,
          &bodyOffset, &chunkOffset);
      if (progress == HttpResponse::HeaderProgress::NeedMore) {
        continue;
      }
      if (progress != HttpResponse::HeaderProgress::Complete) {
        break;
      }
      if (settings->headers_done_fn != nullptr &&
          settings->headers_done_fn(nullptr, settings->callback_arg, headerBuffer,
                                    static_cast<uint16_t>(bodyOffset),
                                    head.contentLength) < 0) {
        break;
      }
      headerComplete = true;
    }
    if (!headerComplete) {
      continue;
    }
    const uint32_t bodyLength = static_cast<uint32_t>(count) - chunkOffset;
    if (bodyLength > 0U) {
      HTTP_buffer_t body{chunk + chunkOffset, static_cast<int32_t>(bodyLength)};
      if (!HttpResponse::acceptsBody(head, received, bodyLength,
                                     settings->max_response_len) ||
          settings->recv_fn == nullptr ||
          settings->recv_fn(settings->recv_fn_arg, &body, 0) < 0) {
        break;
      }
      received += bodyLength;
    }
    if (HttpResponse::isBodyComplete(head, received)) {
      result = HTTP_CLIENT_SUCCESS;
      break;
    }
  }

  closesocket(socketHandle);
  vPortFree(headerBuffer);
  vPortFree(chunk);
  const HTTP_Status_Code_e status = static_cast<HTTP_Status_Code_e>(head.statusCode);
  if (!headerComplete ||
      (head.hasContentLength && !HttpResponse::isBodyComplete(head, received)) ||
      result != HTTP_CLIENT_SUCCESS) {
    notifyFailure(settings, status, received);
    return HTTP_CLIENT_ERR;
  }
  if (settings->result_fn != nullptr) {
    settings->result_fn(settings->callback_arg, status, received, 0U, 0);
  }
  return HTTP_CLIENT_SUCCESS;
}

}  // namespace HostController
