#include <Debug/PulseLed.hpp>

#include <Debug/ActivityLedBridge.h>

#include "cmsis_compiler.h"

namespace {

bool inInterrupt()
{
    return __get_IPSR() != 0U;
}

}  // namespace

PulseLed::PulseLed(GPIO_TypeDef* port, uint16_t pin) : port_(port), pin_(pin) {}

void PulseLed::init()
{
    timer_ = xTimerCreateStatic("PulseLed", 1, pdFALSE, this, &PulseLed::onTimer, &timerBuffer_);
}

void PulseLed::pulse(uint32_t ms)
{
    if (timer_ == nullptr || ms == 0U)
    {
        return;
    }
    const bool isr = inInterrupt();
    const TickType_t now = isr ? xTaskGetTickCountFromISR() : xTaskGetTickCount();
    const TickType_t ticks = pdMS_TO_TICKS(ms) > 0U ? pdMS_TO_TICKS(ms) : 1U;

    // BSRR: a single write, so it is safe from any context.
    port_->BSRR = pin_;

    // Keep the running pulse while it still has at least half of this one
    // left: a burst of short pulses then restarts the timer at most every half
    // pulse instead of flooding its command queue, and a longer pulse (a
    // switch press) always takes over. Judged from the end tick, as asking the
    // timer is not allowed from an interrupt.
    const int32_t remaining = static_cast<int32_t>(endTick_ - now);
    if (remaining > 0 && static_cast<TickType_t>(remaining) >= ticks / 2U)
    {
        return;
    }
    endTick_ = now + ticks;
    if (isr)
    {
        BaseType_t woken = pdFALSE;
        (void)xTimerChangePeriodFromISR(timer_, ticks, &woken);
        portYIELD_FROM_ISR(woken);
    }
    else
    {
        (void)xTimerChangePeriod(timer_, ticks, 0);
    }
}

void PulseLed::onTimer(TimerHandle_t timer)
{
    PulseLed& led = *static_cast<PulseLed*>(pvTimerGetTimerID(timer));
    // BRR: a single write that clears the pin.
    led.port_->BRR = led.pin_;
}

PulseLed& activityLed()
{
    static PulseLed led(LED_2_GPIO_Port, LED_2_Pin);
    return led;
}

extern "C" void ActivityLed_Pulse(uint32_t ms)
{
    activityLed().pulse(ms);
}
