#include <AppVariant.hpp>
#include <Debug/DebugService.hpp>
#include <Console/ConsoleService.hpp>
#include <Device/SCT2xxx.hpp>
#include <Display/BufferedDisplayBoard.hpp>
#include <Display/Display.hpp>
#include <Display/PcbDisplayBoard.hpp>
#include <HostController/MainLoopTask.hpp>
#include <St67HttpFetchTask.hpp>
#include <SwitchTask.hpp>

#include "main.h"

#include <array>

extern SPI_HandleTypeDef hspi3;
extern I2C_HandleTypeDef hi2c1;
extern TIM_HandleTypeDef htim2;

SCT2xxx localSct(
    &hspi3,
    SCT_ENABLE_GPIO_Port,
    SCT_ENABLE_Pin,
    SCT_LATCH_GPIO_Port,
    SCT_LATCH_Pin);

Display::PcbDisplayBoard localBoard(
    localSct,
    htim2,
    {DISPLAY_1_EN_GPIO_Port, DISPLAY_2_EN_GPIO_Port, DISPLAY_3_EN_GPIO_Port,
     DISPLAY_4_EN_GPIO_Port, DISPLAY_5_EN_GPIO_Port},
    {DISPLAY_1_EN_Pin, DISPLAY_2_EN_Pin, DISPLAY_3_EN_Pin,
     DISPLAY_4_EN_Pin, DISPLAY_5_EN_Pin});

Display::BufferedDisplayBoard remoteBoard1(hi2c1, 0x10U);
Display::BufferedDisplayBoard remoteBoard2(hi2c1, 0x11U);
Display::BufferedDisplayBoard remoteBoard3(hi2c1, 0x12U);
Display::BufferedDisplayBoard remoteBoard4(hi2c1, 0x13U);

Display::Display display(
    localBoard,
    {&remoteBoard1, &remoteBoard2, &remoteBoard3, &remoteBoard4});

void AppVariant_Init() {
  DebugService::instance().init();
  DebugService::instance().start();
  ConsoleService::instance().init(&display);
  ConsoleService::instance().start();
    localBoard.start();

    display.local().numeric(0).setFixed(1234, 2);
    display.submit();

  HostController::StartSt67HttpFetchTask();
  MainLoopTask::instance().start();
  SwitchTask::setSwitch1Handler(&MainLoopTask::trigger);
  SwitchTask::setSwitch2Handler(&HostController::TriggerSt67ConnectivityCycle);
}
