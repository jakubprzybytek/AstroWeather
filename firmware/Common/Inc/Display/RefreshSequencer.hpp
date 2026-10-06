#pragma once

#include <Display/DisplayCodec.hpp>

#include <array>
#include <cstdint>

namespace Display {

// Steps the refresh through the passes of every slot and the blink phases,
// and knows how long each pass lasts. Pure: the refresh interrupt asks what
// starts now and what comes after it; the tests drive it directly.
//
// Within a slot the passes run longest first, so the slot switch and its
// settle time come out of the longest pass, not the shortest.
//
// A frame's length can be adjusted, to keep in step with the host (see
// Display/Timeline.hpp): the adjustment is shared out between the five
// slots and lengthens or shortens each slot's first, longest, pass.
class RefreshSequencer {
public:
    static constexpr uint32_t kSlotMicros = 4000U;  // five slots: a 50 Hz frame
    // Frames per blink half-period: 0.5 s on, 0.5 s off at 50 Hz.
    static constexpr uint32_t kBlinkHalfPeriodFrames = 25U;

    struct Step {
        uint8_t slot;
        uint8_t pass;        // index into PassFrames' pass dimension
        uint32_t micros;     // length of this pass
        uint8_t blinkPhase;  // kBlinkOn or kBlinkOff for the frame this is in
        bool firstInSlot;    // the multiplexing slot changes with this pass
        bool firstInFrame;   // the first pass of a frame
    };

    RefreshSequencer();

    // Pass lengths as percentages of a slot, in pass index order, summing to
    // 100. False, and unchanged, otherwise.
    bool setPassPercent(const std::array<uint8_t, kPassCount>& percent);
    const std::array<uint8_t, kPassCount>& passPercent() const { return passPercent_; }
    const std::array<uint32_t, kPassCount>& passMicros() const { return passMicros_; }

    // Advances: the step that starts now. The first call is slot 0's first
    // pass of frame 1.
    Step next();
    // The step after the current one, without advancing.
    Step peek() const;
    Step current() const { return stepFor(state_); }

    // Frames started so far.
    uint32_t frames() const { return state_.frames; }

    // Renumbers the frames by `delta`, without touching the timing: the
    // blink phase follows the new numbers from the next frame on.
    void addFrames(int32_t delta);

    // Microseconds to add to the frame in progress and the frames after it,
    // until set again; negative shortens them. Applies to slots whose first
    // pass has not started yet.
    void setFrameAdjust(int32_t micros) { frameAdjust_ = micros; }

private:
    struct State {
        uint8_t position;  // index into order_
        uint8_t slot;
        uint32_t frames;
    };

    static State advance(State state);
    Step stepFor(const State& state) const;
    int32_t slotAdjust(uint8_t slot) const;
    void sortOrder();

    std::array<uint8_t, kPassCount> passPercent_{};
    std::array<uint32_t, kPassCount> passMicros_{};
    std::array<uint8_t, kPassCount> order_{};  // sequence position -> pass index
    State state_{};
    int32_t frameAdjust_ = 0;
};

} // namespace Display
