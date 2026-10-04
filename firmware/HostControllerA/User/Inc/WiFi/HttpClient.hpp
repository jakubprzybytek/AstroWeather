#ifndef INC_HOSTCONTROLLER_HTTPCLIENT_HPP_
#define INC_HOSTCONTROLLER_HTTPCLIENT_HPP_

#include <stdint.h>

// A bounded, synchronous HTTP/1.1 GET over a W6X_Net socket (the ST67's own
// TCP/IP stack, T01 architecture), plain or TLS. The response is parsed with
// HttpResponseParser and handed to the caller's callbacks; nothing here keeps
// state between requests.

namespace HostController {
namespace HttpClient {

// Return values of get() and the `error` of ResultFn.
constexpr int32_t kSuccess = 0;
constexpr int32_t kBadParam = -1;      // null argument or zero response limit
constexpr int32_t kErrorSocket = -2;   // no socket, or a socket option refused
constexpr int32_t kErrorTls = -3;      // certificate upload, SNI or ALPN refused
constexpr int32_t kErrorConnect = -4;  // TCP connect or TLS handshake failed
constexpr int32_t kErrorSend = -5;     // the request did not go out in full
constexpr int32_t kErrorResponse = -6; // malformed, oversized, truncated, or a callback refused it
constexpr int32_t kErrorTimeout = -7;  // totalTimeoutMs elapsed

const char* resultName(int32_t result);

struct Body {
  const uint8_t* data;
  uint32_t length;
};

// Once, with the complete head (NUL-terminated after `length`) before any
// body; return a negative value to abort the request.
using HeadersFn = int32_t (*)(void* arg, const uint8_t* headers, uint32_t length,
                              uint32_t contentLength);
// For each body fragment, in order; return a negative value to abort.
using BodyFn = int32_t (*)(void* arg, const Body& body);
// Exactly once per get(): the status code (HttpResponse::kNoStatus if no
// status line parsed), body bytes delivered, and the result code.
using ResultFn = void (*)(void* arg, uint32_t statusCode, uint32_t receivedBytes,
                          int32_t error);

// The CA the module verifies the server against. `pem` is uploaded to the
// module's file system under `name` for the duration of the request.
struct Tls {
  const char* name;
  const char* pem;
};

struct Request {
  uint8_t serverIpv4[4] = {};        // from W6X_Net_ResolveHostAddress()
  uint16_t port = 0U;
  const char* host = nullptr;        // Host header and, with TLS, the SNI
  const char* path = nullptr;
  const Tls* tls = nullptr;          // null for plain HTTP
  uint32_t ioTimeoutMs = 0U;         // per send and per receive
  uint32_t totalTimeoutMs = 0U;      // from connect to the last body byte
  uint32_t maxResponseLength = 0U;   // bodies over this are refused
  HeadersFn onHeaders = nullptr;
  BodyFn onBody = nullptr;
  ResultFn onResult = nullptr;
  void* arg = nullptr;
};

int32_t get(const Request& request);

}  // namespace HttpClient
}  // namespace HostController

#endif /* INC_HOSTCONTROLLER_HTTPCLIENT_HPP_ */
