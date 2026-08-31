#include <AppVariant.hpp>
#include <Console/ConsoleService.hpp>

void AppVariant_Init() {
	ConsoleService::instance().init(nullptr);
	ConsoleService::instance().start();
}
