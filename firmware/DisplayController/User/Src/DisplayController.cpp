#include <DisplayController.hpp>

#include <Debug/BlinkingLed.hpp>
#include <Device/SCT2xxx.hpp>
#include <Display/DisplayAddress.hpp>
#include <Display/PcbDisplayBoard.hpp>
#include <DisplayApp.hpp>
#include <I2cTarget.hpp>
#include <Screens.hpp>
#include <Stats.hpp>
#include <Utils/Led.hpp>
#include <Utils/SwitchInput.hpp>

#include "main.h"

extern SPI_HandleTypeDef hspi1;
extern I2C_HandleTypeDef hi2c1;
extern TIM_HandleTypeDef htim6;

volatile DisplayControllerStats g_displayStats = {};

// Heartbeat on LED_1: on for 20 ms every 2 s, as on the host.
static BlinkingLed led1(LED_1_GPIO_Port, LED_1_Pin, 20, 1980, "Led1");

static SCT2xxx sct(&hspi1, SCT_ENABLE_GPIO_Port, SCT_ENABLE_Pin,
                   SCT_LATCH_GPIO_Port, SCT_LATCH_Pin);

static Display::PcbDisplayBoard board(
    sct, htim6,
    {DISPLAY_1_EN_GPIO_Port, DISPLAY_2_EN_GPIO_Port, DISPLAY_3_EN_GPIO_Port,
     DISPLAY_4_EN_GPIO_Port, DISPLAY_5_EN_GPIO_Port},
    {DISPLAY_1_EN_Pin, DISPLAY_2_EN_Pin, DISPLAY_3_EN_Pin, DISPLAY_4_EN_Pin,
     DISPLAY_5_EN_Pin});

// Flashes briefly for every accepted frame.
static Led led2(LED_2_GPIO_Port, LED_2_Pin);

static I2cTarget link(hi2c1);

static DisplayApp app(board, link, led2);

// Runs from main() before osKernelStart(); the tasks started here only run
// once the scheduler is up.
void DisplayController_Init() {
  led1.start();

  // Prepare the self-test before the refresh starts, so the first frames
  // latched are the self-test rather than whatever the drivers held at reset.
  board.setState(DisplayController::allSegmentsState());
  board.submit();
  board.start();

  app.start();
  Utils::SwitchInput::instance().attach(app.getHandle(), DisplayApp::kFlagSwitch1,
                                        DisplayApp::kFlagSwitch2);

  // The address comes from the straps; I2C listens only once it is set, so
  // this board never answers on another board's address.
  const uint16_t address = Display::detectBoardAddress();
  g_displayStats.address = address;
  link.setRecipient(app.getHandle(), DisplayApp::kFlagFrame);
  if (address != 0U) {
    link.begin(address);
  }
}
