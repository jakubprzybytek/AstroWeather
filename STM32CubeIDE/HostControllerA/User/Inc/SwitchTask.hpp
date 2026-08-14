#pragma once

#include <Utils/Task.hpp>

// Main application loop: waits on switch-press flags (set from EXTI ISRs via
// HAL_GPIO_EXTI_Falling_Callback) and blinks LED_2 for a duration specific to
// whichever switch was pressed.
class SwitchTask : public Task<512>
{
public:
    static SwitchTask& instance();

    // ISR-safe: call only from EXTI falling-edge callback context.
    void onSwitch1Pressed();
    void onSwitch2Pressed();

protected:
    void run() override;

private:
    SwitchTask();

    static constexpr uint32_t kFlagSwitch1 = 1u << 0;
    static constexpr uint32_t kFlagSwitch2 = 1u << 1;

    static constexpr uint32_t kSwitch1BlinkMs = 250;
    static constexpr uint32_t kSwitch2BlinkMs = 50;
};
