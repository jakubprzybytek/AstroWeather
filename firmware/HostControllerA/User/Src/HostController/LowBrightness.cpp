#include <HostController/LowBrightness.hpp>

#include "main.h"

#include <atomic>

namespace LowBrightness {
namespace {

std::atomic<bool> enabled{false};

} // namespace

void set(bool on)
{
    enabled.store(on);
    HAL_GPIO_WritePin(LOW_POWER_EN_GPIO_Port, LOW_POWER_EN_Pin,
                      on ? GPIO_PIN_SET : GPIO_PIN_RESET);
}

bool isEnabled()
{
    return enabled.load();
}

bool toggle()
{
    // Switch 2 toggles from MainLoopTask while the console sets from its own
    // task. A press racing a command just ends with one of the two; no lock.
    const bool on = !enabled.load();
    set(on);
    return on;
}

} // namespace LowBrightness
