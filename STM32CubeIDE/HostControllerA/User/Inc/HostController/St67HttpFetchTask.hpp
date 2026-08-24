#ifndef INC_HOSTCONTROLLER_ST67HTTPFETCHTASK_HPP_
#define INC_HOSTCONTROLLER_ST67HTTPFETCHTASK_HPP_

#include <St67FetchTypes.hpp>

namespace HostController {

void StartSt67HttpFetchTask();
void TriggerSt67SmokeTest();
void TriggerSt67ConnectivityCycle();
bool FetchSt67Data(St67FetchRequest* request);

}  // namespace HostController

#endif /* INC_HOSTCONTROLLER_ST67HTTPFETCHTASK_HPP_ */