#ifndef INC_HOSTCONTROLLER_ST67HTTPRULES_HPP_
#define INC_HOSTCONTROLLER_ST67HTTPRULES_HPP_

#include <stddef.h>
#include <stdint.h>

// The checks St67HttpFetcher applies around a GET: the configured host and
// path, and the response's Content-Type. Pure, so the native tests run them.

namespace HostController {
namespace St67HttpRules {

// A bare host name: non-empty, at most maxHostLength characters, no scheme,
// no port and no CR/LF. The path must start with '/' and have no CR/LF.
bool isValidTarget(const char* host, const char* path, size_t maxHostLength);

enum class ContentTypeCheck : uint8_t {
  Missing,   // no "Content-Type:" in the headers
  Mismatch,  // present, but its value does not start with the expected type
  Match,
};

// Looks for "Content-Type:" (case-sensitive) within the first `length` bytes
// of `headers`, skips spaces and tabs, and checks the value starts with
// `expected`. Anything after the expected prefix is ignored.
ContentTypeCheck checkContentType(const uint8_t* headers, uint16_t length,
                                  const char* expected);

}  // namespace St67HttpRules
}  // namespace HostController

#endif /* INC_HOSTCONTROLLER_ST67HTTPRULES_HPP_ */
