#ifndef INC_HOSTCONTROLLER_ST67HTTPFETCHER_HPP_
#define INC_HOSTCONTROLLER_ST67HTTPFETCHER_HPP_

#include <stdint.h>

#include <Astro/ApiTarget.hpp>

#include "http_client.h"

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
  // HTTP settings keep a pointer to the host for the whole request.
  ApiTarget target_{};
};

}  // namespace HostController

#endif /* INC_HOSTCONTROLLER_ST67HTTPFETCHER_HPP_ */
