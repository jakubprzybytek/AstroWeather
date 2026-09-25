#ifndef INC_HOSTCONTROLLER_HTTPRESPONSEPARSER_HPP_
#define INC_HOSTCONTROLLER_HTTPRESPONSEPARSER_HPP_

#include <stdint.h>

// The socket-free half of HttpClient_Get(): finding the end of the response
// headers, reading the status line and Content-Length, and the body limits.
// No LwIP or FreeRTOS types, so it runs in the native tests.

namespace HostController {
namespace HttpResponse {

// Header bytes the client buffers before giving up, including any body bytes
// that arrive in the same read as the blank line.
constexpr uint32_t kHeaderCapacity = 2048U;

// Status code HttpClient_Get() reports when no status line was parsed
// (HTTP_VERSION_NOT_SUPPORTED in http_client.h).
constexpr uint32_t kNoStatus = 505U;

struct Head {
  uint32_t statusCode = kNoStatus;
  uint32_t contentLength = 0U;
  bool hasContentLength = false;
};

// Finds the "\r\n\r\n" that ends the headers. On success *offset is the index
// of the first body byte.
bool findHeaderEnd(const uint8_t* data, uint32_t length, uint32_t* offset);

// Parses "HTTP/x.y nnn" (nnn up to 599) and any "Content-Length:" header.
// `headers` must be NUL-terminated at or after `length`. statusCode is written
// as soon as the status line parses, even when a later header is malformed.
// Header names are matched case-sensitively; the last Content-Length wins.
bool parseHead(const uint8_t* headers, uint32_t length, Head* head);

enum class HeaderProgress : uint8_t {
  NeedMore,  // no blank line yet; read more
  Complete,  // head parsed; body starts at *bodyOffsetInChunk
  Overflow,  // the headers do not fit in the buffer
  Malformed, // bad status line or Content-Length
  TooLarge,  // Content-Length over maxResponseLength
};

// Appends one read to the header buffer (capacity + 1 bytes, kept
// NUL-terminated) and, once the blank line has arrived, parses the head. On
// Complete, *headEnd is the length of the head in `buffer` and
// *bodyOffsetInChunk is where the body starts within `chunk`.
HeaderProgress appendHeaderBytes(uint8_t* buffer, uint32_t capacity,
                                 uint32_t* bufferLength, const uint8_t* chunk,
                                 uint32_t count, uint32_t maxResponseLength,
                                 Head* head, uint32_t* headEnd,
                                 uint32_t* bodyOffsetInChunk);

// Whether `bodyLength` more body bytes may be accepted after `received`.
bool acceptsBody(const Head& head, uint32_t received, uint32_t bodyLength,
                 uint32_t maxResponseLength);

// Whether the body is complete: exactly Content-Length bytes received.
bool isBodyComplete(const Head& head, uint32_t received);

// Whether the server closing the connection ends the response successfully:
// only when there is no Content-Length to satisfy.
bool closeEndsBody(const Head& head);

// Any 2xx is a success (the fetcher's rule; HttpClient_Get() itself returns
// any status to the caller).
bool isSuccessStatus(uint32_t statusCode);

}  // namespace HttpResponse
}  // namespace HostController

#endif /* INC_HOSTCONTROLLER_HTTPRESPONSEPARSER_HPP_ */
