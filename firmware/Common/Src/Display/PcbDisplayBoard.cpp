#include <Display/PcbDisplayBoard.hpp>

#include <Utils/MicroClock.hpp>

#include "FreeRTOS.h"
#include "task.h"

namespace Display {
namespace {

// Time for a slot's high-side switch to turn fully off before the drivers'
// outputs come back on. The Si2333DDS P-MOSFET is switched on hard through a
// BC847, but off only by the gate pull-up resistor, which is slow; if the
// outputs return too early the old slot still conducts and shows a faint copy
// of the new slot's pattern (ghosting). Raise this if ghosting is visible.
constexpr uint32_t kSlotSettleMicros = 10U;

// SysTick runs at the core clock and counts down; the kernel keeps it going.
uint32_t sysTickElapsed(uint32_t from, uint32_t to)
{
    const uint32_t reload = SysTick->LOAD + 1U;
    return (from >= to) ? (from - to) : (from + reload - to);
}

// Busy-waits on SysTick, so the delay does not depend on compiler
// optimisation. Short waits only; safe in an interrupt.
void delayMicros(uint32_t micros)
{
    // Never spin on a stopped counter.
    if ((SysTick->CTRL & SysTick_CTRL_ENABLE_Msk) == 0U) {
        return;
    }
    const uint32_t target = micros * (SystemCoreClock / 1000000U);
    uint32_t elapsed = 0U;
    uint32_t last = SysTick->VAL;
    while (elapsed < target) {
        const uint32_t now = SysTick->VAL;
        elapsed += sysTickElapsed(last, now);
        last = now;
    }
}

} // namespace

PcbDisplayBoard* PcbDisplayBoard::activeBoard_ = nullptr;

PcbDisplayBoard::PcbDisplayBoard(
    SCT2xxx& driver, TIM_HandleTypeDef& timer,
    const std::array<GPIO_TypeDef*, kSlotCount>& enablePorts,
    const std::array<uint16_t, kSlotCount>& enablePins)
    : driver_(driver), timer_(timer), enablePorts_(enablePorts), enablePins_(enablePins),
      front_(&frameSets_[0]), back_(&frameSets_[1])
{
    activeBoard_ = this;
}

void PcbDisplayBoard::start()
{
    driver_.enable();
    // The first interrupt starts slot 0; until then the drivers hold what
    // they had, with every slot switch off.
    __HAL_TIM_SET_AUTORELOAD(&timer_, sequencer_.peek().micros - 1U);
    __HAL_TIM_SET_COUNTER(&timer_, 0U);
    // Frame 0 lasts until the first interrupt.
    records_[0] = {0U, Utils::microsNow(), kFrameMicros, 0U, 0};
    activeRecord_ = 0U;
    started_ = true;
    HAL_TIM_Base_Start_IT(&timer_);
}

void PcbDisplayBoard::submit()
{
    show(state_, attributes_);
}

void PcbDisplayBoard::show(const LogicalBoardState& state, const BoardAttributes& attributes)
{
    MutexGuard guard(submitMutex_);
    // While pendingSwap_ is clear the interrupt leaves back_ alone, so the
    // encode cannot be swapped in half-done. A submission still waiting to
    // be shown is simply overwritten by this newer one.
    pendingSwap_ = false;
    PassFrames* const target = back_;
    encodePasses(state, attributes, *target);
    pendingSwap_ = true;
}

bool PcbDisplayBoard::setPassPercent(const std::array<uint8_t, kPassCount>& percent)
{
    // The interrupt reads the table; change it in one go.
    taskENTER_CRITICAL();
    const bool accepted = sequencer_.setPassPercent(percent);
    taskEXIT_CRITICAL();
    return accepted;
}

bool PcbDisplayBoard::stampNow(TimelineStamp& stamp)
{
    PcbDisplayBoard* const board = activeBoard_;
    if (board == nullptr || !board->started_) {
        return false;
    }
    const uint32_t primask = __get_PRIMASK();
    __disable_irq();
    stamp = board->records_[board->activeRecord_];
    // Read live, not from the record: applySync() changes it mid-frame, and a
    // sync applied against the record's stale copy was corrected twice
    // (bench, 2026-10-07).
    stamp.pendingMicros = board->servo_.pending();
    stamp.now = Utils::microsNow();
    __set_PRIMASK(primask);
    return true;
}

void PcbDisplayBoard::applySync(int32_t jumpFrames, int32_t phaseMicros, int32_t pendingAtStamp,
                                int32_t ratePpm)
{
    taskENTER_CRITICAL();
    sequencer_.addFrames(jumpFrames);
    // The record the next stampNow() reads is renumbered too: the interrupt
    // only rewrites it at the next frame start, and a sync stamped before
    // that would jump by the same frames again (bench, 2026-10-07).
    records_[activeRecord_].frame += static_cast<uint32_t>(jumpFrames);
    const int32_t correctedSince = pendingAtStamp - servo_.pending();
    servo_.setPending(phaseMicros - correctedSince);
    servo_.setRatePpm(ratePpm);
    taskEXIT_CRITICAL();
}

bool PcbDisplayBoard::refreshStats(RefreshStats& stats) const
{
    stats = {sequencer_.frames(), lateShifts_, lateInterrupts_, maxInterruptMicros_,
             sequencer_.passPercent()};
    return true;
}

void PcbDisplayBoard::onTimerElapsed(TIM_HandleTypeDef* timer)
{
    if (activeBoard_ != nullptr && timer == &activeBoard_->timer_) {
        activeBoard_->onPass();
    }
}

void PcbDisplayBoard::switchSlot(uint8_t slot)
{
    // Swap slots with the outputs blanked, so neither slot shows the
    // other's data while the switches change over.
    driver_.disable();
    HAL_GPIO_WritePin(enablePorts_[activeSlot_], enablePins_[activeSlot_], GPIO_PIN_SET);
    driver_.latch();
    HAL_GPIO_WritePin(enablePorts_[slot], enablePins_[slot], GPIO_PIN_RESET);
    delayMicros(kSlotSettleMicros);
    driver_.enable();
    activeSlot_ = slot;
}

// The refresh timer's update: one pass ends, the next begins.
void PcbDisplayBoard::onPass()
{
    const uint32_t entered = SysTick->VAL;
    RefreshSequencer::Step now = sequencer_.next();
    if (now.firstInFrame) {
        startFrame();
        now = sequencer_.current();
    }

    // 1. Show the pass that starts now: its data was shifted in during the
    //    pass that just ended. If that shift is somehow still running, the
    //    interrupt is more than a pass late; keep the old data rather than
    //    latch half of the new.
    if (driver_.busy()) {
        lateShifts_ = lateShifts_ + 1U;
    } else if (now.firstInSlot) {
        switchSlot(now.slot);
    } else {
        driver_.latch();
    }

    // 2. This pass's length. Preload is off, so it applies to the period
    //    that has just started. If this interrupt came later than the pass
    //    is long (a long critical section elsewhere), the counter is already
    //    past the new reload and would run on to the timer's full range
    //    before the next update, 71 minutes on the 32-bit TIM2. Restart the
    //    pass instead: it shows for its length plus the delay.
    __HAL_TIM_SET_AUTORELOAD(&timer_, now.micros - 1U);
    if (__HAL_TIM_GET_COUNTER(&timer_) >= now.micros - 1U) {
        __HAL_TIM_SET_COUNTER(&timer_, 0U);
        lateInterrupts_ = lateInterrupts_ + 1U;
    }

    // 3. Shift the next pass's data in the background. A new submission is
    //    taken over only at a frame boundary, so every frame comes from one.
    const RefreshSequencer::Step following = sequencer_.peek();
    if (following.firstInFrame && pendingSwap_) {
        PassFrames* const shown = front_;
        front_ = back_;
        back_ = shown;
        pendingSwap_ = false;
    }
    if (!driver_.busy()) {
        const PassFrames& frames = *front_;
        driver_.shiftDma(&frames[following.blinkPhase][following.pass][following.slot * kBytesPerSlot],
                         kBytesPerSlot);
    }

    const uint32_t micros = sysTickElapsed(entered, SysTick->VAL) / (SystemCoreClock / 1000000U);
    if (micros > maxInterruptMicros_) {
        maxInterruptMicros_ = micros;
    }
}

// A frame starts: its length, LED_1 and the record stampNow() reads.
void PcbDisplayBoard::startFrame()
{
    const int32_t adjust = servo_.nextFrame();
    sequencer_.setFrameAdjust(adjust);
    const uint32_t frame = sequencer_.frames();
    HAL_GPIO_WritePin(LED_1_GPIO_Port, LED_1_Pin,
                      heartbeatLit(frame, heartbeatTwice_) ? GPIO_PIN_SET : GPIO_PIN_RESET);

    // The counter has run since the update that started the frame.
    const uint32_t sinceStart = __HAL_TIM_GET_COUNTER(&timer_);
    const uint8_t spare = static_cast<uint8_t>(activeRecord_ ^ 1U);
    records_[spare] = {frame, Utils::microsNow() - sinceStart,
                       static_cast<uint32_t>(static_cast<int32_t>(kFrameMicros) + adjust), 0U, 0};
    activeRecord_ = spare;
}

} // namespace Display

extern "C" void Display_PcbTimerElapsed(TIM_HandleTypeDef* timer)
{
    Display::PcbDisplayBoard::onTimerElapsed(timer);
}
