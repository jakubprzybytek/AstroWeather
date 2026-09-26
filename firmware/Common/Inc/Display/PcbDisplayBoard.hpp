#pragma once

#include <Display/DisplayBoard.hpp>
#include <Display/DisplayCodec.hpp>
#include <Display/RefreshSequencer.hpp>
#include <Device/SCT2xxx.hpp>
#include <Utils/Mutex.hpp>

#include "main.h"

#include <array>
#include <cstdint>

namespace Display {

// The board's own display. submit() encodes the content and attributes into
// a set of frames, one per blink phase and pass; the refresh timer's
// interrupt then shows them, one pass per interrupt: it latches the data the
// DMA shifted in during the previous pass, sets the length of the pass that
// starts now, and starts the DMA for the next one. No task is involved, so
// the timing depends only on interrupt latency.
class PcbDisplayBoard : public DisplayBoard {
public:
    PcbDisplayBoard(SCT2xxx& driver, TIM_HandleTypeDef& timer,
                    const std::array<GPIO_TypeDef*, kSlotCount>& enablePorts,
                    const std::array<uint16_t, kSlotCount>& enablePins);

    void start();
    void submit() override;

    bool setPassPercent(const std::array<uint8_t, kPassCount>& percent) override;
    bool refreshStats(RefreshStats& stats) const override;

    static void onTimerElapsed(TIM_HandleTypeDef* timer);

private:
    static PcbDisplayBoard* activeBoard_;

    void onPass();
    void switchSlot(uint8_t slot);

    SCT2xxx& driver_;
    TIM_HandleTypeDef& timer_;
    std::array<GPIO_TypeDef*, kSlotCount> enablePorts_;
    std::array<uint16_t, kSlotCount> enablePins_;

    RefreshSequencer sequencer_;
    // Double-buffered: the interrupt reads front_, submit() writes back_ and
    // asks for a swap, which the interrupt does before it shifts the first
    // pass of a frame, so a frame never mixes two submissions.
    std::array<PassFrames, 2> frameSets_{};
    PassFrames* volatile front_;
    PassFrames* volatile back_;
    volatile bool pendingSwap_ = false;
    Mutex submitMutex_;

    uint8_t activeSlot_ = 0U;
    volatile uint32_t lateShifts_ = 0U;
    volatile uint32_t lateInterrupts_ = 0U;
    volatile uint32_t maxInterruptMicros_ = 0U;
};

} // namespace Display

extern "C" void Display_PcbTimerElapsed(TIM_HandleTypeDef* timer);
