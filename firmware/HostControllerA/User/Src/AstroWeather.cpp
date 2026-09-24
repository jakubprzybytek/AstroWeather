#include <AstroWeather.hpp>

#include <main.h>

#include <AppVariant.hpp>
#include <Debug/BlinkingLed.hpp>

static BlinkingLed led1(LED_1_GPIO_Port, LED_1_Pin, 20, 1980, "Led1");

void AstroWeather_Init() {
  // BlinkingLeds::start();

  led1.start();
  AppVariant_Init();
}
