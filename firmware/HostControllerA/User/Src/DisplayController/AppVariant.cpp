#include <AppVariant.hpp>
#include <Console/ConsoleService.hpp>

#include "main.h"

void AppVariant_Init() {
	// LOW_POWER_ENABLE is bussed to every board and driven by the host. The
	// shared MX_GPIO_Init() makes it a push-pull output driven low, which would
	// fight the host's high level, so release it (Hardware_Review.md M-4).
	GPIO_InitTypeDef lowPowerEnable = {};
	lowPowerEnable.Pin = LOW_POWER_EN_Pin;
	lowPowerEnable.Mode = GPIO_MODE_INPUT;
	lowPowerEnable.Pull = GPIO_NOPULL;
	HAL_GPIO_Init(LOW_POWER_EN_GPIO_Port, &lowPowerEnable);

	ConsoleService::instance().init(nullptr);
	ConsoleService::instance().start();
}
