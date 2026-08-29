#include <Display/PcbDisplayBoard.hpp>
#include <Display/DisplayCodec.hpp>
#include <Debug/DebugService.hpp>

namespace Display {

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
        HAL_GPIO_WritePin(enablePorts_[activeSlot_], enablePins_[activeSlot_], GPIO_PIN_SET);
        const uint8_t nextSlot = static_cast<uint8_t>((activeSlot_ + 1U) % kSlotCount);
        if (driver_.send(&frame_[nextSlot * kBytesPerSlot], kBytesPerSlot) == HAL_OK) {
            HAL_GPIO_WritePin(enablePorts_[nextSlot], enablePins_[nextSlot], GPIO_PIN_RESET);
            activeSlot_ = nextSlot;
            if (activeSlot_ == 0U) {
                const uint32_t now = HAL_GetTick();
                if (cycleMeasurementStarted_) {
                    const uint32_t cycleMs = now - cycleStartedAt_;
                    ++completedCycles_;
                    if ((completedCycles_ % 50U) == 0U) {
                        DebugService::instance().logf(
                            DebugService::Level::Info,
                            "Display refresh cycle: %lu ms",
                            static_cast<unsigned long>(cycleMs));
                    }
                } else {
                    cycleMeasurementStarted_ = true;
                }
                cycleStartedAt_ = now;
            }
        }
    }
}

} // namespace Display

extern "C" void Display_PcbTimerElapsed(TIM_HandleTypeDef* timer)
{
    Display::PcbDisplayBoard::onTimerElapsed(timer);
}
