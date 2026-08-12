#include <AstroWeather.hpp>

#include <main.h>

#include <AppVariant.hpp>
#include <Misc/BlinkingLeds.hpp>
#include <Debug/BlinkingLed.hpp>

static BlinkingLed led1(LED_1_GPIO_Port, LED_1_Pin, 5000, "Led1");
static BlinkingLed led2(LED_2_GPIO_Port, LED_2_Pin, 2000, "Led2");

void AstroWeather_Init() {
  // BlinkingLeds::start();

  led1.start();
  led2.start();

  AppVariant_Init();
}
