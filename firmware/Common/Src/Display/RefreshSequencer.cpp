#include <Display/RefreshSequencer.hpp>

namespace Display {

RefreshSequencer::RefreshSequencer()
{
    std::array<uint8_t, kPassCount> percent{};
    for (uint8_t pass = 0; pass < kPassCount; ++pass) {
        percent[pass] = kPassPercent[pass];
    }
    setPassPercent(percent);
    // So that the first next() lands on slot 0, position 0, frame 1.
    state_ = {static_cast<uint8_t>(kPassCount - 1U), static_cast<uint8_t>(kSlotCount - 1U), 0U};
}

bool RefreshSequencer::setPassPercent(const std::array<uint8_t, kPassCount>& percent)
{
    uint32_t total = 0U;
    for (const uint8_t value : percent) {
        if (value == 0U) {
            return false;
        }
        total += value;
    }
    if (total != 100U) {
        return false;
    }
    for (uint8_t pass = 0; pass < kPassCount; ++pass) {
        passMicros_[pass] = kSlotMicros * percent[pass] / 100U;
    }
    // Integer rounding: whatever is left goes to the first pass, so the
    // passes still make up the whole slot.
    uint32_t used = 0U;
    for (const uint32_t micros : passMicros_) {
        used += micros;
    }
    passMicros_[0] += kSlotMicros - used;
    sortOrder();
    return true;
}

void RefreshSequencer::sortOrder()
{
    for (uint8_t pass = 0; pass < kPassCount; ++pass) {
        order_[pass] = pass;
    }
    // Longest first; a stable insertion sort keeps equal lengths in index order.
    for (uint8_t i = 1; i < kPassCount; ++i) {
        const uint8_t pass = order_[i];
        uint8_t j = i;
        while (j > 0U && passMicros_[order_[j - 1U]] < passMicros_[pass]) {
            order_[j] = order_[j - 1U];
            --j;
        }
        order_[j] = pass;
    }
}

RefreshSequencer::State RefreshSequencer::advance(State state)
{
    if (++state.position >= kPassCount) {
        state.position = 0U;
        if (++state.slot >= kSlotCount) {
            state.slot = 0U;
            ++state.frames;
        }
    }
    return state;
}

RefreshSequencer::Step RefreshSequencer::stepFor(const State& state) const
{
    Step step{};
    step.slot = state.slot;
    step.pass = order_[state.position];
    step.micros = passMicros_[step.pass];
    // Frames 1..25 are the on phase, 26..50 the off phase, and so on.
    step.blinkPhase =
        (state.frames != 0U && (((state.frames - 1U) / kBlinkHalfPeriodFrames) & 1U) != 0U)
            ? kBlinkOff
            : kBlinkOn;
    step.firstInSlot = state.position == 0U;
    step.firstInFrame = step.firstInSlot && state.slot == 0U;
    return step;
}

RefreshSequencer::Step RefreshSequencer::next()
{
    state_ = advance(state_);
    return stepFor(state_);
}

RefreshSequencer::Step RefreshSequencer::peek() const
{
    return stepFor(advance(state_));
}

} // namespace Display
