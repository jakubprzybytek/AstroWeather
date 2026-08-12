#include <AppVariant.hpp>
#include <Debug/DebugService.hpp>
#include <St67ProbeTask.hpp>

// TODO: start the WiFi communication task and the display driving task
void AppVariant_Init() {
  DebugService::instance().init();
  DebugService::instance().start();
  HostController::StartSt67ProbeTask();
}
