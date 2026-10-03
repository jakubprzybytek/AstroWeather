#pragma once

#include "main.h"  // GPIO_TypeDef

#include "FreeRTOS.h"
#include "timers.h"

#include <cstdint>

// An LED lit for a while by pulse(), from a task or an interrupt, without
// blocking: the pin is set at once and a one-shot timer clears it. Pulses
// that overlap merge, so the LED stays lit until the latest one ends. LED2
// shows switch presses (long pulses) and USB CDC traffic (short ones); see
// docs/Console.md.
class PulseLed
{
public:
    PulseLed(GPIO_TypeDef* port, uint16_t pin);

    // Creates the timer. Call once, before or after the scheduler starts;
    // pulses before that are ignored.
    void init();

    // Lights the LED for `ms`, or longer if an earlier pulse still runs.
    void pulse(uint32_t ms);

private:
    static void onTimer(TimerHandle_t timer);

    GPIO_TypeDef* port_;
    uint16_t pin_;
    TimerHandle_t timer_ = nullptr;
    StaticTimer_t timerBuffer_{};
    volatile TickType_t endTick_ = 0;
};

// The board's LED2.
PulseLed& activityLed();
