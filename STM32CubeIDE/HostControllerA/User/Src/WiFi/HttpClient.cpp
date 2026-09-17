#include <HostController/HttpClient.hpp>

#include "lwip/inet.h"
#include "lwip/sockets.h"
#include "FreeRTOS.h"

#include <cstdio>
#include <cstring>

namespace HostController {
namespace {

constexpr uint32_t kHeaderCapacity = 2048U;
constexpr uint32_t kChunkCapacity = 1024U;

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

bool findHeaderEnd(const uint8_t* data, uint32_t length, uint32_t* offset) {
  if (length < 4U) {
    return false;
  }
  for (uint32_t index = 3U; index < length; ++index) {
    if (data[index - 3U] == '\r' && data[index - 2U] == '\n' &&
        data[index - 1U] == '\r' && data[index] == '\n') {
      *offset = index + 1U;
      return true;
    }
  }
  return false;
}

bool parseResponse(const uint8_t* headers, uint32_t length,
                   HTTP_Status_Code_e* status, uint32_t* contentLength,
                   bool* hasContentLength) {
  const char* text = reinterpret_cast<const char*>(headers);
  unsigned int code = 0U;
  if (std::sscanf(text, "HTTP/%*u.%*u %u", &code) != 1 || code > 599U) {
    return false;
  }
  *status = static_cast<HTTP_Status_Code_e>(code);
  *contentLength = 0U;
  *hasContentLength = false;
  const char* end = text + length;
  const char* line = std::strstr(text, "\r\n");
  while (line != nullptr && line + 2 < end) {
    line += 2;
    if (line[0] == '\r' && line[1] == '\n') {
      break;
    }
    if (std::strncmp(line, "Content-Length:", 15U) == 0) {
      const char* value = line + 15U;
      while (value < end && (*value == ' ' || *value == '\t')) {
        ++value;
      }
      char* parsedEnd = nullptr;
      unsigned long parsed = std::strtoul(value, &parsedEnd, 10);
      if (parsedEnd == value || parsed > UINT32_MAX ||
          (parsedEnd < end && *parsedEnd != '\r')) {
        return false;
      }
      *contentLength = static_cast<uint32_t>(parsed);
      *hasContentLength = true;
    }
    const char* next = std::strstr(line, "\r\n");
    if (next == nullptr || next + 2 > end) {
      break;
    }
    line = next;
  }
  return true;
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
  uint32_t contentLength = 0U;
  uint32_t received = 0U;
  bool hasContentLength = false;
  HTTP_Status_Code_e status = HTTP_VERSION_NOT_SUPPORTED;
  bool headerComplete = false;
  int32_t result = HTTP_CLIENT_ERR;

  while (true) {
    int32_t count = recv(socketHandle, chunk, kChunkCapacity, 0);
    if (count == 0) {
      if (headerComplete && !hasContentLength) {
        result = HTTP_CLIENT_SUCCESS;
      }
      break;
    }
    if (count < 0) {
      break;
    }
    uint32_t chunkOffset = 0U;
    if (!headerComplete) {
      if (headerLength + static_cast<uint32_t>(count) > kHeaderCapacity) {
        break;
      }
      std::memcpy(headerBuffer + headerLength, chunk, static_cast<size_t>(count));
      headerLength += static_cast<uint32_t>(count);
      headerBuffer[headerLength] = 0U;
      if (!findHeaderEnd(headerBuffer, headerLength, &bodyOffset)) {
        continue;
      }
      if (!parseResponse(headerBuffer, bodyOffset, &status, &contentLength,
                         &hasContentLength)) {
        break;
      }
      if (hasContentLength && contentLength > settings->max_response_len) {
        break;
      }
      if (settings->headers_done_fn != nullptr &&
          settings->headers_done_fn(nullptr, settings->callback_arg, headerBuffer,
                                    static_cast<uint16_t>(bodyOffset),
                                    contentLength) < 0) {
        break;
      }
      headerComplete = true;
      const uint32_t previousLength = headerLength - static_cast<uint32_t>(count);
      chunkOffset = bodyOffset > previousLength ? bodyOffset - previousLength :
                         static_cast<uint32_t>(count);
    }
    if (!headerComplete) {
      continue;
    }
    const uint32_t bodyLength = static_cast<uint32_t>(count) - chunkOffset;
    if (bodyLength > 0U) {
      HTTP_buffer_t body{chunk + chunkOffset, static_cast<int32_t>(bodyLength)};
      if (received > settings->max_response_len - bodyLength ||
          (hasContentLength && received + bodyLength > contentLength) ||
          settings->recv_fn == nullptr ||
          settings->recv_fn(settings->recv_fn_arg, &body, 0) < 0) {
        break;
      }
      received += bodyLength;
    }
    if (hasContentLength && received == contentLength) {
      result = HTTP_CLIENT_SUCCESS;
      break;
    }
  }

  closesocket(socketHandle);
  vPortFree(headerBuffer);
  vPortFree(chunk);
  if (!headerComplete || (hasContentLength && received != contentLength) ||
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
