#include <AstroWeather.hpp>

#include <Astro/AstroDataRefreshTask.hpp>
#include <Clock/ClockTask.hpp>
#include <Console/ConsoleService.hpp>
#include <Debug/LogService.hpp>
#include <Debug/PulseLed.hpp>
#include <Device/HsiTrim.hpp>
#include <Device/Eeprom24AA04.hpp>
#include <Device/I2cBus.hpp>
#include <Device/SCT2xxx.hpp>
#include <Display/BootScreens.hpp>
#include <Display/BufferedDisplayBoard.hpp>
#include <Display/DisplayAddress.hpp>
#include <Display/DisplaySyncTask.hpp>
#include <Display/Display.hpp>
#include <Display/LowBrightness.hpp>
#include <Display/PcbDisplayBoard.hpp>
#include <MainLoopTask.hpp>
#include <Sensors/CurrentSenseTask.hpp>
#include <Settings/SettingsStore.hpp>
#include <Utils/SwitchInput.hpp>
#include <WiFi/St67HttpFetchTask.hpp>

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

// One remote board per forecast block, 0x10 to 0x15 (Display/BoardChain.hpp).
// The one at the host's own address is never used: the local board shows that
// block instead.
Display::BufferedDisplayBoard remoteBoard10(i2c1Bus, 0x10U);
Display::BufferedDisplayBoard remoteBoard11(i2c1Bus, 0x11U);
Display::BufferedDisplayBoard remoteBoard12(i2c1Bus, 0x12U);
Display::BufferedDisplayBoard remoteBoard13(i2c1Bus, 0x13U);
Display::BufferedDisplayBoard remoteBoard14(i2c1Bus, 0x14U);
Display::BufferedDisplayBoard remoteBoard15(i2c1Bus, 0x15U);

Display::Display display(localBoard, {&remoteBoard10, &remoteBoard11,
                                      &remoteBoard12, &remoteBoard13,
                                      &remoteBoard14, &remoteBoard15});

Device::Eeprom24AA04 settingsEeprom(i2c1Bus);

Settings::Store settingsStore(settingsEeprom);


// Runs from main() before osKernelStart(), so nothing here may block on the
// scheduler; the tasks started below only run once it is up.
void AstroWeather_Init() {
  // LED_1 is the heartbeat, driven by localBoard's refresh interrupt on the
  // timeline the display boards follow (Display/Timeline.hpp).
  // LED2: switch presses and USB CDC traffic; before the USB device starts.
  activityLed().init();

  LogService::instance().init();
  LogService::instance().start();

  // The host's place in the chain comes from its straps, as on a display
  // board; before anything submits, so no frame goes to its own address.
  display.setLocalAddress(Display::detectBoardAddress());
  if (display.localInChain()) {
    LogService::instance().logf(LogService::Level::Info,
                                "Display address 0x%02X: forecast block %u",
                                static_cast<unsigned>(display.localAddress()),
                                static_cast<unsigned>(Display::chainPosition(display.localAddress())));
  } else {
    LogService::instance().logf(LogService::Level::Warn,
                                "Display address 0x%02X is outside 0x10-0x15: no forecast block",
                                static_cast<unsigned>(display.localAddress()));
  }

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

  // The HSI trim measured on the bench ('time hsi'); before the refresh
  // starts, so the host's timeline runs at the trimmed rate from frame 1.
  if (settingsStore.values().hsiTrim != HsiTrim::kDefault) {
    HsiTrim::set(settingsStore.values().hsiTrim);
    LogService::instance().logf(LogService::Level::Info, "HSI trim %u",
                                static_cast<unsigned>(settingsStore.values().hsiTrim));
  }

  CurrentSenseTask::instance().setLoggingEnabled(settingsStore.values().adcLogEnabled);
  CurrentSenseTask::instance().setDisplayEnabled(settingsStore.values().adcDisplayEnabled);
  CurrentSenseTask::instance().setDisplay(&display);
  CurrentSenseTask::instance().start();
  ConsoleService::instance().init(&display);
  ConsoleService::instance().setEeprom(&settingsEeprom);
  ConsoleService::instance().setSettings(&settingsStore);
  ConsoleService::instance().start();
  // Before the displays light up, so a saved low brightness applies from the first frame.
  LowBrightness::set(settingsStore.values().lowBrightness);
  // "No data" until the first astro refresh; the clock and the current
  // readout then take over their own numeric displays. It shows once
  // MainLoopTask has run the boot screens; the first slot-test frame is
  // latched before the refresh starts, rather than whatever the drivers held
  // at reset.
  localBoard.setState(Display::noDataState());
  localBoard.show(Display::slotTestState(0U), Display::BoardAttributes{});
  localBoard.start();
  ClockTask::instance().setDisplayEnabled(settingsStore.values().clockDisplayEnabled);
  // A saved trim beyond RtcTrim::kMaxTrimPpm was measured for the LSI the RTC
  // ran from before HSE / 32 (+19300 ppm on the first board). Applied to the
  // crystal it would make the clock 2 % wrong, so it is dropped here, in RAM:
  // the EEPROM cannot be written before the scheduler runs, and the next save
  // of any setting writes 0.
  if (!RtcTrim::isValidPpm(settingsStore.values().clockTrimPpm)) {
    LogService::instance().logf(LogService::Level::Warn,
                                "Clock trim %+ld ppm was for the LSI; using 0",
                                static_cast<long>(settingsStore.values().clockTrimPpm));
    settingsStore.values().clockTrimPpm = 0;
  }
  if (!ClockTask::instance().setTrim(settingsStore.values().clockTrimPpm)) {
    LogService::instance().logf(LogService::Level::Error, "Clock trim %ld ppm not applied",
                                static_cast<long>(settingsStore.values().clockTrimPpm));
  }
  ClockTask::instance().setDisplay(&display);
  ClockTask::instance().start();
  // After localBoard.start(): the sync carries its refresh timeline.
  DisplaySyncTask::instance().init(&i2c1Bus, &display);
  DisplaySyncTask::instance().start();

  // Before the fetch task starts: it reads the credentials on every connect.
  HostController::SetSt67CredentialSource(&settingsStore);
  HostController::StartSt67HttpFetchTask();
  HostController::AstroDataRefreshTask::instance().init(&display);
  HostController::AstroDataRefreshTask::instance().start();
  MainLoopTask::instance().init(activityLed(), &settingsStore, &display);
  MainLoopTask::instance().start();
  Utils::SwitchInput::instance().attach(
      MainLoopTask::instance().getHandle(), MainLoopTask::kEventSwitch1,
      MainLoopTask::kEventSwitch2);
}
