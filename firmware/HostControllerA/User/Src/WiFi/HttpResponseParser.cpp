#include <HostController/HttpResponseParser.hpp>

#include <cstdio>
#include <cstdlib>
#include <cstring>

namespace HostController {
namespace HttpResponse {

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

bool parseHead(const uint8_t* headers, uint32_t length, Head* head) {
  const char* text = reinterpret_cast<const char*>(headers);
  unsigned int code = 0U;
  if (std::sscanf(text, "HTTP/%*u.%*u %u", &code) != 1 || code > 599U) {
    return false;
  }
  head->statusCode = code;
  head->contentLength = 0U;
  head->hasContentLength = false;
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
      head->contentLength = static_cast<uint32_t>(parsed);
      head->hasContentLength = true;
    }
    const char* next = std::strstr(line, "\r\n");
    if (next == nullptr || next + 2 > end) {
      break;
    }
    line = next;
  }
  return true;
}

HeaderProgress appendHeaderBytes(uint8_t* buffer, uint32_t capacity,
                                 uint32_t* bufferLength, const uint8_t* chunk,
                                 uint32_t count, uint32_t maxResponseLength,
                                 Head* head, uint32_t* headEnd,
                                 uint32_t* bodyOffsetInChunk) {
  if (*bufferLength + count > capacity) {
    return HeaderProgress::Overflow;
  }
  std::memcpy(buffer + *bufferLength, chunk, static_cast<size_t>(count));
  *bufferLength += count;
  buffer[*bufferLength] = 0U;
  if (!findHeaderEnd(buffer, *bufferLength, headEnd)) {
    return HeaderProgress::NeedMore;
  }
  if (!parseHead(buffer, *headEnd, head)) {
    return HeaderProgress::Malformed;
  }
  if (head->hasContentLength && head->contentLength > maxResponseLength) {
    return HeaderProgress::TooLarge;
  }
  const uint32_t previousLength = *bufferLength - count;
  *bodyOffsetInChunk = *headEnd > previousLength ? *headEnd - previousLength : count;
  return HeaderProgress::Complete;
}

bool acceptsBody(const Head& head, uint32_t received, uint32_t bodyLength,
                 uint32_t maxResponseLength) {
  return !(received > maxResponseLength - bodyLength ||
           (head.hasContentLength && received + bodyLength > head.contentLength));
}

bool isBodyComplete(const Head& head, uint32_t received) {
  return head.hasContentLength && received == head.contentLength;
}

bool closeEndsBody(const Head& head) { return !head.hasContentLength; }

bool isSuccessStatus(uint32_t statusCode) {
  return statusCode >= 200U && statusCode < 300U;
}

}  // namespace HttpResponse
}  // namespace HostController
