#include <DisplayController.hpp>

#include <Debug/PulseLed.hpp>
#include <Device/SCT2xxx.hpp>
#include <Display/BootScreens.hpp>
#include <Display/DisplayAddress.hpp>
#include <Display/PcbDisplayBoard.hpp>
#include <DisplayApp.hpp>
#include <I2cTarget.hpp>
#include <Stats.hpp>
#include <TimelineFollower.hpp>
#include <Utils/SwitchInput.hpp>

#include "main.h"

extern SPI_HandleTypeDef hspi1;
extern I2C_HandleTypeDef hi2c1;
extern TIM_HandleTypeDef htim6;

volatile DisplayControllerStats g_displayStats = {};

static SCT2xxx sct(&hspi1, SCT_ENABLE_GPIO_Port, SCT_ENABLE_Pin,
                   SCT_LATCH_GPIO_Port, SCT_LATCH_Pin);

static Display::PcbDisplayBoard board(
    sct, htim6,
    {DISPLAY_1_EN_GPIO_Port, DISPLAY_2_EN_GPIO_Port, DISPLAY_3_EN_GPIO_Port,
     DISPLAY_4_EN_GPIO_Port, DISPLAY_5_EN_GPIO_Port},
    {DISPLAY_1_EN_Pin, DISPLAY_2_EN_Pin, DISPLAY_3_EN_Pin, DISPLAY_4_EN_Pin,
     DISPLAY_5_EN_Pin});

static I2cTarget link(hi2c1);

static TimelineFollower timeline(board, link);

static DisplayApp app(board, link, timeline);

// Runs from main() before osKernelStart(); the tasks started here only run
// once the scheduler is up.
void DisplayController_Init() {
  // LED_1 is the heartbeat, driven by the refresh interrupt on the
  // timeline it shares with the host (Display/Timeline.hpp).
  // LED_2: every write addressed to this board; before listening starts.
  activityLed().init();

  // Prepare the first slot-test frame before the refresh starts, so the
  // first frames latched are the test rather than whatever the drivers held
  // at reset. DisplayApp runs the rest of the boot screens.
  board.show(Display::slotTestState(0U), Display::BoardAttributes{});
  board.start();

  app.start();
  Utils::SwitchInput::instance().attach(app.getHandle(), DisplayApp::kFlagSwitch1,
                                        DisplayApp::kFlagSwitch2);

  // The address comes from the straps; I2C listens only once it is set, so
  // this board never answers on another board's address.
  const uint16_t address = Display::detectBoardAddress();
  g_displayStats.address = address;
  timeline.init();
  link.setRecipient(app.getHandle(), DisplayApp::kFlagFrame, DisplayApp::kFlagSync);
  if (address != 0U) {
    link.begin(address);
  }
}
