#ifndef INC_HOSTCONTROLLER_ST67HTTPFETCHER_HPP_
#define INC_HOSTCONTROLLER_ST67HTTPFETCHER_HPP_

#include <stdint.h>

#include <Astro/ApiTarget.hpp>

namespace HostController {

struct St67Runtime;
struct St67FetchRequest;

class St67HttpFetcher {
 public:
  explicit St67HttpFetcher(St67Runtime& runtime);

  bool fetch(St67FetchRequest* request);

 private:
  St67Runtime& runtime_;
  // Resolved at the start of each fetch. A member, not a local, because the
  // HTTP request keeps a pointer to the host for the whole request.
  ApiTarget target_{};
  // target_.path with "?key=<key>" appended; what the request line carries.
  char requestPath_[Settings::kMaxApiPathLength + 5U + Settings::kMaxApiKeyLength + 1U]{};
};

}  // namespace HostController

#endif /* INC_HOSTCONTROLLER_ST67HTTPFETCHER_HPP_ */
