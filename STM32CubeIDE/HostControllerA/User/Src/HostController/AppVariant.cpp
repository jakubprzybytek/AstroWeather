#include <AppVariant.hpp>
#include <Debug/DebugService.hpp>

// TODO: start the WiFi communication task and the display driving task
void AppVariant_Init() {
  DebugService::instance().init();
  DebugService::instance().start();
}
