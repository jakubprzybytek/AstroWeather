#pragma once

#include <Utils/Task.hpp>

#include <cstdint>

class Led;

// Main application loop: waits on switch-press flags (set from EXTI ISRs via
// HAL_GPIO_EXTI_Falling_Callback) and blinks led2 for a duration specific to
// whichever switch was pressed.
class SwitchTask : public Task<512>
{
public:
    explicit SwitchTask(Led& led2);

    // ISR-safe: routes to whichever SwitchTask instance is currently active.
    static void handleExtiFalling(uint16_t gpioPin);

protected:
    void run() override;

private:
    void onSwitch1Pressed();
    void onSwitch2Pressed();

    static constexpr uint32_t kFlagSwitch1 = 1u << 0;
    static constexpr uint32_t kFlagSwitch2 = 1u << 1;

    static constexpr uint32_t kSwitch1BlinkMs = 250;
    static constexpr uint32_t kSwitch2BlinkMs = 50;

    Led& led2_;
    static SwitchTask* s_activeInstance;
};
