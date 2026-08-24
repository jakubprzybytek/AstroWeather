#ifndef INC_HOSTCONTROLLER_ST67HTTPFETCHER_HPP_
#define INC_HOSTCONTROLLER_ST67HTTPFETCHER_HPP_

#include <stdint.h>

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
};

}  // namespace HostController

#endif /* INC_HOSTCONTROLLER_ST67HTTPFETCHER_HPP_ */
