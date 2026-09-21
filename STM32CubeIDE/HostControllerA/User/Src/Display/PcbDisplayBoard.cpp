#include <Display/PcbDisplayBoard.hpp>
#include <Display/DisplayCodec.hpp>

namespace Display {
namespace {

// Time for a slot's high-side switch to turn fully off before the drivers'
// outputs come back on. The Si2333DDS P-MOSFET is switched on hard through a
// BC847, but off only by the gate pull-up resistor, which is slow; if the
// outputs return too early the old slot still conducts and shows a faint copy
// of the new slot's pattern (ghosting). Raise this if ghosting is visible.
constexpr uint32_t kSlotSettleMicros = 10U;

// Busy-waits using SysTick, which the kernel keeps running at the core clock,
// so the delay does not depend on compiler optimisation. Short waits only.
void delayMicros(uint32_t micros)
{
    // Never spin on a stopped counter: that would hang the display task.
    if ((SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) == 0U) {
        return;
    }
    const uint32_t reload = SysTick->LOAD + 1U;
    const uint32_t target = micros * (SystemCoreClock / 1000000U);
    uint32_t elapsed = 0U;
    uint32_t last = SysTick->VAL;
    while (elapsed < target) {
        const uint32_t now = SysTick->VAL;
        // SysTick counts down and reloads.
        elapsed += (last >= now) ? (last - now) : (last + reload - now);
        last = now;
    }
}

} // namespace

PcbDisplayBoard* PcbDisplayBoard::activeBoard_ = nullptr;

PcbDisplayBoard::PcbDisplayBoard(
    SCT2xxx& driver, TIM_HandleTypeDef& timer,
    const std::array<GPIO_TypeDef*, kSlotCount>& enablePorts,
    const std::array<uint16_t, kSlotCount>& enablePins)
    : Task<1024>("DisplayRefresh", osPriorityRealtime),
      driver_(driver), timer_(timer), enablePorts_(enablePorts), enablePins_(enablePins)
{
    activeBoard_ = this;
}

void PcbDisplayBoard::start()
{
    driver_.enable();
    Task<1024>::start();
    HAL_TIM_Base_Start_IT(&timer_);
}

void PcbDisplayBoard::submit()
{
    MutexGuard guard(frameMutex_);
    encodePcb(state_, frame_);
}

void PcbDisplayBoard::onTimerElapsed(TIM_HandleTypeDef* timer)
{
    if (activeBoard_ != nullptr && timer == &activeBoard_->timer_) {
        osThreadFlagsSet(activeBoard_->getHandle(), kRefreshFlag);
    }
}

void PcbDisplayBoard::run()
{
    for (;;) {
        const uint32_t flags = osThreadFlagsWait(kRefreshFlag, osFlagsWaitAny, osWaitForever);
        if ((flags & osFlagsError) != 0U) {
            continue;
        }
        const uint8_t nextSlot = static_cast<uint8_t>((activeSlot_ + 1U) % kSlotCount);

        // Shift the next slot's data in while the current slot stays lit: with
        // LA/ low the drivers' outputs keep the current data, so the display
        // is dark only for the swap below, not for the whole transfer.
        HAL_StatusTypeDef status;
        {
            MutexGuard guard(frameMutex_);
            status = driver_.shift(&frame_[nextSlot * kBytesPerSlot], kBytesPerSlot);
        }
        if (status != HAL_OK) {
            // Keep showing the current slot and try again on the next tick.
            continue;
        }

        // Swap slots with the outputs blanked, so neither slot shows the
        // other's data while the switches change over.
        driver_.disable();
        HAL_GPIO_WritePin(enablePorts_[activeSlot_], enablePins_[activeSlot_], GPIO_PIN_SET);
        driver_.latch();
        HAL_GPIO_WritePin(enablePorts_[nextSlot], enablePins_[nextSlot], GPIO_PIN_RESET);
        delayMicros(kSlotSettleMicros);
        driver_.enable();
        activeSlot_ = nextSlot;
    }
}

} // namespace Display

extern "C" void Display_PcbTimerElapsed(TIM_HandleTypeDef* timer)
{
    Display::PcbDisplayBoard::onTimerElapsed(timer);
}
