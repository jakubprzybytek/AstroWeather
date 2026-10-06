#pragma once

#include <Display/DisplayBoard.hpp>
#include <Display/DisplayCodec.hpp>
#include <Display/RefreshSequencer.hpp>
#include <Display/Timeline.hpp>
#include <Display/TimelineServo.hpp>
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
//
// The frames are the board's timeline (Display/Timeline.hpp): at each frame
// start the interrupt lights LED_1 for the heartbeat frame, applies the
// servo's correction to the frame's length, and records when the frame
// started, for stampNow(). On the host the servo stays at zero: its refresh
// is the reference the display boards follow.
class PcbDisplayBoard : public DisplayBoard {
public:
    PcbDisplayBoard(SCT2xxx& driver, TIM_HandleTypeDef& timer,
                    const std::array<GPIO_TypeDef*, kSlotCount>& enablePorts,
                    const std::array<uint16_t, kSlotCount>& enablePins);

    void start();
    void submit() override;
    void show(const LogicalBoardState& state, const BoardAttributes& attributes) override;

    bool setPassPercent(const std::array<uint8_t, kPassCount>& percent) override;
    bool refreshStats(RefreshStats& stats) const override;

    static void onTimerElapsed(TIM_HandleTypeDef* timer);

    // The timeline now, from any interrupt or task; false before start().
    static bool stampNow(TimelineStamp& stamp);

    // Applies a TimelineSync decision: renumber the frames, set the phase
    // still to correct (counted from a stamp whose pending correction was
    // `pendingAtStamp`; what the servo has corrected since is taken off) and
    // the rate.
    void applySync(int32_t jumpFrames, int32_t phaseMicros, int32_t pendingAtStamp,
                   int32_t ratePpm);
    int32_t ratePpm() const { return servo_.ratePpm(); }

private:
    static PcbDisplayBoard* activeBoard_;

    void onPass();
    void startFrame();
    void switchSlot(uint8_t slot);

    SCT2xxx& driver_;
    TIM_HandleTypeDef& timer_;
    std::array<GPIO_TypeDef*, kSlotCount> enablePorts_;
    std::array<uint16_t, kSlotCount> enablePins_;

    RefreshSequencer sequencer_;
    TimelineServo servo_;
    // When each frame started, double-buffered: the interrupt writes the
    // spare and then flips activeRecord_, so a reader in a higher-priority
    // interrupt never sees half a record.
    std::array<TimelineStamp, 2> records_{};
    volatile uint8_t activeRecord_ = 0U;
    volatile bool started_ = false;
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
