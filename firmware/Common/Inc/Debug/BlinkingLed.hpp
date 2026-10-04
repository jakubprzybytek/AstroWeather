#pragma once

#include <Utils/Task.hpp>
#include "main.h"  // GPIO_TypeDef, HAL_GPIO_* types

// Blinks an active-high LED: on for onMs, then off for offMs, repeatedly.
class BlinkingLed : public Task<768>
{
public:
    // The heartbeat both boards show on LED_1 from power-up: 20 ms every 2 s.
    static constexpr uint32_t kHeartbeatOnMs = 20U;
    static constexpr uint32_t kHeartbeatOffMs = 1980U;

    BlinkingLed(GPIO_TypeDef* port, uint16_t pin, uint32_t onMs, uint32_t offMs,
                const char* name = "BlinkingLed");

    void init();

protected:
    void run() override;

private:
    GPIO_TypeDef* port_;
    uint16_t pin_;
    uint32_t onMs_;
    uint32_t offMs_;
};
