#pragma once

#include <Display/DisplayBoard.hpp>
#include <Device/SCT2xxx.hpp>
#include <Utils/Task.hpp>

#include "main.h"

#include <array>
#include <cstdint>

namespace Display {

class PcbDisplayBoard : public DisplayBoard, public Task<1024> {
public:
    PcbDisplayBoard(SCT2xxx& driver, TIM_HandleTypeDef& timer,
                    const std::array<GPIO_TypeDef*, kSlotCount>& enablePorts,
                    const std::array<uint16_t, kSlotCount>& enablePins);

    void start();
    void submit() override;
    static void onTimerElapsed(TIM_HandleTypeDef* timer);

protected:
    void run() override;

private:
    static PcbDisplayBoard* activeBoard_;
    static constexpr uint32_t kRefreshFlag = 1U << 0;

    SCT2xxx& driver_;
    TIM_HandleTypeDef& timer_;
    std::array<GPIO_TypeDef*, kSlotCount> enablePorts_;
    std::array<uint16_t, kSlotCount> enablePins_;
    PreparedFrame frame_{};
    uint8_t activeSlot_ = 0;
};

} // namespace Display

extern "C" void Display_PcbTimerElapsed(TIM_HandleTypeDef* timer);
