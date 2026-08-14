#include <AstroWeather.hpp>

#include <main.h>

#include <AppVariant.hpp>
#include <Debug/BlinkingLed.hpp>
#include <SwitchTask.hpp>
#include <Utils/Led.hpp>

static BlinkingLed led1(LED_1_GPIO_Port, LED_1_Pin, 2000, "Led1");
// static BlinkingLed led2(LED_2_GPIO_Port, LED_2_Pin, 2000, "Led2");

// Used by SwitchTask (extern) to blink on switch presses.
Led led2(LED_2_GPIO_Port, LED_2_Pin);

void AstroWeather_Init() {
  // BlinkingLeds::start();

  led1.start();
  // led2.start();

  SwitchTask::instance().start();

  AppVariant_Init();
}
