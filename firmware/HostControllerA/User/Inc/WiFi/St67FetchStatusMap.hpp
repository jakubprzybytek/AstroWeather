#ifndef INC_HOSTCONTROLLER_ST67FETCHSTATUSMAP_HPP_
#define INC_HOSTCONTROLLER_ST67FETCHSTATUSMAP_HPP_

#include <WiFi/St67FetchTypes.hpp>

namespace HostController {

// The status a client fetch reports, from the first stage that failed
// (runtime.firstFailureStage; nullptr when nothing failed) and whether the
// body overflowed the caller's buffer. The overflow wins over any stage.
St67FetchStatus fetchStatusForFailure(const char* firstFailureStage, bool responseTooLarge);

}  // namespace HostController

#endif /* INC_HOSTCONTROLLER_ST67FETCHSTATUSMAP_HPP_ */
