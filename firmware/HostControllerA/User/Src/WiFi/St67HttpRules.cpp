#include <WiFi/St67HttpRules.hpp>

#include <cstdio>
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

bool isValidKey(const char* key, size_t maxLength) {
  const size_t length = std::strlen(key);
  if (length == 0U || length > maxLength) {
    return false;
  }
  for (size_t i = 0U; i < length; ++i) {
    const char c = key[i];
    const bool unreserved = (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') ||
                            (c >= '0' && c <= '9') || c == '-' || c == '_' || c == '.' ||
                            c == '~';
    if (!unreserved) {
      return false;
    }
  }
  return true;
}

bool formatRequestPath(char* out, size_t size, const char* path, const char* key) {
  if (size == 0U) {
    return false;
  }
  const int written = (key[0] == '\0')
      ? std::snprintf(out, size, "%s", path)
      : std::snprintf(out, size, "%s%ckey=%s", path,
                      (std::strchr(path, '?') != nullptr) ? '&' : '?', key);
  if (written < 0 || static_cast<size_t>(written) >= size) {
    out[0] = '\0';
    return false;
  }
  return true;
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
