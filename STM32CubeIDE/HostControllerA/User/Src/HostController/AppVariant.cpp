#include <AppVariant.hpp>
#include <Console/AstroCommand.hpp>
#include <Console/ConsoleService.hpp>
#include <Debug/LogService.hpp>
#include <Device/Eeprom24AA04.hpp>
#include <Device/I2cBus.hpp>
#include <Device/SCT2xxx.hpp>
#include <Display/BufferedDisplayBoard.hpp>
#include <Display/Display.hpp>
#include <Display/PcbDisplayBoard.hpp>
#include <HostController/MainLoopTask.hpp>
#include <HostController/AstroDataRefreshTask.hpp>
#include <Debug/LogService.hpp>
#include <Sensors/CurrentSenseTask.hpp>
#include <Settings/SettingsStore.hpp>
#include <St67HttpFetchTask.hpp>
#include <Utils/Led.hpp>
#include <Utils/SwitchInput.hpp>

#include "main.h"

#include <array>

extern SPI_HandleTypeDef hspi3;
extern I2C_HandleTypeDef hi2c1;
extern TIM_HandleTypeDef htim2;

// Declared before its clients: within a translation unit static objects are
// constructed in declaration order, and the boards below hold a reference to it.
Device::I2cBus i2c1Bus(hi2c1);

SCT2xxx localSct(&hspi3, SCT_ENABLE_GPIO_Port, SCT_ENABLE_Pin,
                 SCT_LATCH_GPIO_Port, SCT_LATCH_Pin);

Display::PcbDisplayBoard localBoard(
    localSct, htim2,
    {DISPLAY_1_EN_GPIO_Port, DISPLAY_2_EN_GPIO_Port, DISPLAY_3_EN_GPIO_Port,
     DISPLAY_4_EN_GPIO_Port, DISPLAY_5_EN_GPIO_Port},
    {DISPLAY_1_EN_Pin, DISPLAY_2_EN_Pin, DISPLAY_3_EN_Pin, DISPLAY_4_EN_Pin,
     DISPLAY_5_EN_Pin});

Display::BufferedDisplayBoard remoteBoard1(i2c1Bus, 0x10U);
Display::BufferedDisplayBoard remoteBoard2(i2c1Bus, 0x11U);
Display::BufferedDisplayBoard remoteBoard3(i2c1Bus, 0x12U);
Display::BufferedDisplayBoard remoteBoard4(i2c1Bus, 0x13U);
Display::BufferedDisplayBoard remoteBoard5(i2c1Bus, 0x14U);

Display::Display display(localBoard, {&remoteBoard1, &remoteBoard2,
                                      &remoteBoard3, &remoteBoard4,
                                      &remoteBoard5});

Device::Eeprom24AA04 settingsEeprom(i2c1Bus);

Settings::Store settingsStore(settingsEeprom);

Led led2(LED_2_GPIO_Port, LED_2_Pin);

void AppVariant_Init() {
  LogService::instance().init();
  LogService::instance().start();

  // Runs before osKernelStart(); reads take no osDelay and the bus mutex is
  // uncontended here, so this does not block. Log the outcome rather than the
  // values, since they include credentials.
  const Settings::LoadResult loaded = settingsStore.load();
  LogService::instance().logf(
      LogService::Level::Info, "Settings load=%s stored=%s",
      (loaded == Settings::LoadResult::Ok)
          ? "ok"
          : ((loaded == Settings::LoadResult::ReadFailed) ? "read-failed" : "defaulted"),
      Settings::Store::describe(settingsStore.lastDecode()));

  CurrentSenseTask::instance().setLoggingEnabled(settingsStore.values().adcLogEnabled);
  CurrentSenseTask::instance().setDisplayEnabled(settingsStore.values().adcDisplayEnabled);
  CurrentSenseTask::instance().setDisplay(&display);
  CurrentSenseTask::instance().start();
  ConsoleService::instance().init(&display);
  ConsoleService::instance().setEeprom(&settingsEeprom);
  ConsoleService::instance().setSettings(&settingsStore);
  ConsoleService::instance().start();
  localBoard.start();

  // Before the fetch task starts: it reads the credentials on every connect.
  HostController::SetSt67CredentialSource(&settingsStore);
  HostController::StartSt67HttpFetchTask();
  HostController::AstroDataRefreshTask::instance().init(&display);
  HostController::AstroDataRefreshTask::instance().start();
  MainLoopTask::instance().init(led2);
  MainLoopTask::instance().start();
  Utils::SwitchInput::instance().attach(
      MainLoopTask::instance().getHandle(), MainLoopTask::kEventSwitch1,
      MainLoopTask::kEventSwitch2);
}
