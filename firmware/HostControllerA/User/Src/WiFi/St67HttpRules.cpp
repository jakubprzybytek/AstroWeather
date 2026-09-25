#include <WiFi/St67HttpRules.hpp>

#include <cstring>

namespace HostController {
namespace St67HttpRules {
namespace {

constexpr char kContentTypeName[] = "Content-Type:";

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

}  // namespace

bool isValidTarget(const char* host, const char* path, size_t maxHostLength) {
  // Whitespace and CR/LF would break the request line or the Host header.
  return !(std::strlen(host) == 0U || std::strlen(host) > maxHostLength ||
           std::strstr(host, "://") != nullptr || std::strpbrk(host, ":/ \t\r\n") != nullptr ||
           std::strlen(path) == 0U || path[0] != '/' ||
           std::strpbrk(path, " \t\r\n") != nullptr);
}

ContentTypeCheck checkContentType(const uint8_t* headers, uint16_t length,
                                  const char* expected) {
  const char* contentType = findBounded(headers, length, kContentTypeName);
  if (contentType == nullptr) {
    return ContentTypeCheck::Missing;
  }
  const char* value = contentType + std::strlen(kContentTypeName);
  const char* end = reinterpret_cast<const char*>(headers) + length;
  while (value < end && (*value == ' ' || *value == '\t')) {
    ++value;
  }
  const size_t expectedLength = std::strlen(expected);
  if (value + expectedLength > end || std::memcmp(value, expected, expectedLength) != 0) {
    return ContentTypeCheck::Mismatch;
  }
  return ContentTypeCheck::Match;
}

}  // namespace St67HttpRules
}  // namespace HostController
