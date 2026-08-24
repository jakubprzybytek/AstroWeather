#include <AppVariant.hpp>
#include <Debug/DebugService.hpp>
#include <HostController/MainLoopTask.hpp>
#include <St67HttpFetchTask.hpp>
#include <SwitchTask.hpp>

void AppVariant_Init() {
  DebugService::instance().init();
  DebugService::instance().start();
  HostController::StartSt67HttpFetchTask();
  MainLoopTask::instance().start();
  SwitchTask::setSwitch1Handler(&MainLoopTask::trigger);
  SwitchTask::setSwitch2Handler(nullptr);
}
