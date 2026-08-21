#include <AppVariant.hpp>
#include <Debug/DebugService.hpp>
#include <St67ServiceTask.hpp>
#include <SwitchTask.hpp>

void AppVariant_Init() {
  DebugService::instance().init();
  DebugService::instance().start();
  HostController::StartSt67ServiceTask();
  SwitchTask::setSwitch1Handler(&HostController::TriggerSt67SmokeTest);
  SwitchTask::setSwitch2Handler(nullptr);
}
