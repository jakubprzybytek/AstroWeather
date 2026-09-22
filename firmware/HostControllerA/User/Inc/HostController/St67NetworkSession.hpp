#ifndef INC_HOSTCONTROLLER_ST67NETWORKSESSION_HPP_
#define INC_HOSTCONTROLLER_ST67NETWORKSESSION_HPP_

#include <stdint.h>

#include "cmsis_os2.h"
#include "w6x_api.h"

namespace HostController {

struct St67Runtime;

class St67NetworkSession {
 public:
  explicit St67NetworkSession(St67Runtime& runtime);

  bool initialize(bool logModule);
  bool open();
  bool disconnect();
  bool stop();
  bool isReady() const;
  bool isStationDisconnected() const;

 private:
  St67Runtime& runtime_;
};

}  // namespace HostController

#endif /* INC_HOSTCONTROLLER_ST67NETWORKSESSION_HPP_ */
